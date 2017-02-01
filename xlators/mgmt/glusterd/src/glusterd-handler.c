/*
   Copyright (c) 2006-2012 Red Hat, Inc. <http://www.redhat.com>
   This file is part of GlusterFS.

   This file is licensed to you under your choice of the GNU Lesser
   General Public License, version 3 or any later version (LGPLv3 or
   later), or the GNU General Public License, version 2 (GPLv2), in all
   cases as published by the Free Software Foundation.
*/
#include <inttypes.h>

#include "globals.h"
#include "glusterfs.h"
#include "compat.h"
#include "dict.h"
#include "protocol-common.h"
#include "xlator.h"
#include "logging.h"
#include "syscall.h"
#include "timer.h"
#include "defaults.h"
#include "compat.h"
#include "compat-errno.h"
#include "statedump.h"
#include "run.h"
#include "glusterd-mem-types.h"
#include "glusterd.h"
#include "glusterd-sm.h"
#include "glusterd-op-sm.h"
#include "glusterd-utils.h"
#include "glusterd-server-quorum.h"
#include "glusterd-store.h"
#include "glusterd-locks.h"
#include "glusterd-snapshot-utils.h"

#include "glusterd1-xdr.h"
#include "cli1-xdr.h"
#include "xdr-generic.h"
#include "rpc-clnt.h"
#include "glusterd-volgen.h"
#include "glusterd-mountbroker.h"
#include "glusterd-messages.h"
#include "glusterd-errno.h"

#include <sys/resource.h>
#include <inttypes.h>

#include "common-utils.h"

#include "globals.h"
#include "glusterd-syncop.h"
#include "glusterd-messages.h"

#ifdef HAVE_BD_XLATOR
#include <lvm2app.h>
#endif

extern glusterd_op_info_t opinfo;
static int volcount;

int glusterd_big_locked_notify (struct rpc_clnt *rpc, void *mydata,
                                rpc_clnt_event_t event,
                                void *data, rpc_clnt_notify_t notify_fn)
{
        glusterd_conf_t *priv = THIS->private;
        int              ret   = -1;

        synclock_lock (&priv->big_lock);
        ret = notify_fn (rpc, mydata, event, data);
        synclock_unlock (&priv->big_lock);

        return ret;
}

int glusterd_big_locked_handler (rpcsvc_request_t *req, rpcsvc_actor actor_fn)
{
        glusterd_conf_t *priv = THIS->private;
        int             ret   = -1;

        synclock_lock (&priv->big_lock);
        ret = actor_fn (req);
        synclock_unlock (&priv->big_lock);

        return ret;
}

static int
glusterd_handle_friend_req (rpcsvc_request_t *req, uuid_t  uuid,
                            char *hostname, int port,
                            gd1_mgmt_friend_req *friend_req)
{
        int                             ret = -1;
        glusterd_peerinfo_t             *peerinfo = NULL;
        glusterd_friend_sm_event_t      *event = NULL;
        glusterd_friend_req_ctx_t       *ctx = NULL;
        char                            rhost[UNIX_PATH_MAX + 1] = {0};
        uuid_t                          friend_uuid = {0};
        dict_t                          *dict = NULL;

        gf_uuid_parse (uuid_utoa (uuid), friend_uuid);
        if (!port)
                port = GF_DEFAULT_BASE_PORT;

        ret = glusterd_remote_hostname_get (req, rhost, sizeof (rhost));

        rcu_read_lock ();

        peerinfo = glusterd_peerinfo_find (uuid, rhost);

        if (peerinfo == NULL) {
                gf_event (EVENT_PEER_REJECT, "peer=%s", hostname);
                ret = glusterd_xfer_friend_add_resp (req, hostname, rhost, port,
                                                     -1, GF_PROBE_UNKNOWN_PEER);
                if (friend_req->vols.vols_val) {
                        free (friend_req->vols.vols_val);
                        friend_req->vols.vols_val = NULL;
                }
                goto out;
        }

        ret = glusterd_friend_sm_new_event
                        (GD_FRIEND_EVENT_RCVD_FRIEND_REQ, &event);

        if (ret) {
                gf_msg ("glusterd", GF_LOG_ERROR, 0,
                        GD_MSG_EVENT_NEW_GET_FAIL,
                        "event generation failed: %d", ret);
                goto out;
        }

        event->peername = gf_strdup (peerinfo->hostname);
        gf_uuid_copy (event->peerid, peerinfo->uuid);

        ctx = GF_CALLOC (1, sizeof (*ctx), gf_gld_mt_friend_req_ctx_t);

        if (!ctx) {
                gf_msg ("glusterd", GF_LOG_ERROR, ENOMEM,
                        GD_MSG_NO_MEMORY, "Unable to allocate memory");
                ret = -1;
                goto out;
        }

        gf_uuid_copy (ctx->uuid, uuid);
        if (hostname)
                ctx->hostname = gf_strdup (hostname);
        ctx->req = req;

        dict = dict_new ();
        if (!dict) {
                ret = -1;
                goto out;
        }

        ret = dict_unserialize (friend_req->vols.vols_val,
                                friend_req->vols.vols_len,
                                &dict);

        if (ret)
                goto out;
        else
                dict->extra_stdfree = friend_req->vols.vols_val;

        ctx->vols = dict;
        event->ctx = ctx;

        ret = glusterd_friend_sm_inject_event (event);
        if (ret) {
                gf_msg ("glusterd", GF_LOG_ERROR, 0,
                        GD_MSG_EVENT_INJECT_FAIL,
                        "Unable to inject event %d, "
                        "ret = %d", event->event, ret);
                goto out;
        }

        ret = 0;
        if (peerinfo && (0 == peerinfo->connected))
                ret = GLUSTERD_CONNECTION_AWAITED;

out:
        rcu_read_unlock ();

        if (ret && (ret != GLUSTERD_CONNECTION_AWAITED)) {
                if (ctx && ctx->hostname)
                        GF_FREE (ctx->hostname);
                GF_FREE (ctx);
                if (dict) {
                        if ((!dict->extra_stdfree) &&
                            friend_req->vols.vols_val)
                                free (friend_req->vols.vols_val);
                        dict_unref (dict);
                } else {
                    free (friend_req->vols.vols_val);
                }
                if (event)
                        GF_FREE (event->peername);
                GF_FREE (event);
        }


        return ret;
}

static int
glusterd_handle_unfriend_req (rpcsvc_request_t *req, uuid_t  uuid,
                              char *hostname, int port)
{
        int                             ret = -1;
        glusterd_peerinfo_t             *peerinfo = NULL;
        glusterd_friend_sm_event_t      *event = NULL;
        glusterd_friend_req_ctx_t       *ctx = NULL;

        if (!port)
                port = GF_DEFAULT_BASE_PORT;

        rcu_read_lock ();

        peerinfo = glusterd_peerinfo_find (uuid, hostname);

        if (peerinfo == NULL) {
                gf_msg ("glusterd", GF_LOG_CRITICAL, 0,
                        GD_MSG_REQ_FROM_UNKNOWN_PEER,
                        "Received remove-friend from unknown peer %s",
                        hostname);
                ret = glusterd_xfer_friend_remove_resp (req, hostname,
                                                        port);
                goto out;
        }

        ret = glusterd_friend_sm_new_event
                        (GD_FRIEND_EVENT_RCVD_REMOVE_FRIEND, &event);

        if (ret) {
                gf_msg ("glusterd", GF_LOG_ERROR, 0,
                        GD_MSG_EVENT_NEW_GET_FAIL,
                        "event generation failed: %d", ret);
                goto out;
        }

        event->peername = gf_strdup (hostname);
        gf_uuid_copy (event->peerid, uuid);

        ctx = GF_CALLOC (1, sizeof (*ctx), gf_gld_mt_friend_req_ctx_t);

        if (!ctx) {
                gf_msg ("glusterd", GF_LOG_ERROR, ENOMEM,
                        GD_MSG_NO_MEMORY, "Unable to allocate memory");
                ret = -1;
                goto out;
        }

        gf_uuid_copy (ctx->uuid, uuid);
        if (hostname)
                ctx->hostname = gf_strdup (hostname);
        ctx->req = req;

        event->ctx = ctx;

        ret = glusterd_friend_sm_inject_event (event);

        if (ret) {
                gf_msg ("glusterd", GF_LOG_ERROR, 0,
                        GD_MSG_EVENT_INJECT_FAIL, "Unable to inject event %d, "
                        "ret = %d", event->event, ret);
                goto out;
        }

        ret = 0;

out:
        rcu_read_unlock ();

        if (0 != ret) {
                if (ctx && ctx->hostname)
                        GF_FREE (ctx->hostname);
                GF_FREE (ctx);
                if (event)
                        GF_FREE (event->peername);
                GF_FREE (event);
        }

        return ret;
}

struct args_pack {
    dict_t *dict;
    int vol_count;
    int opt_count;
};

static int
_build_option_key (dict_t *d, char *k, data_t *v, void *tmp)
{
        char                    reconfig_key[256] = {0, };
        struct args_pack        *pack             = NULL;
        int                     ret               = -1;
        xlator_t                *this             = NULL;
        glusterd_conf_t         *priv             = NULL;

        this = THIS;
        GF_ASSERT (this);
        priv = this->private;
        GF_ASSERT (priv);

        pack = tmp;
        if (strcmp (k, GLUSTERD_GLOBAL_OPT_VERSION) == 0)
                return 0;

        if (priv->op_version > GD_OP_VERSION_MIN) {
                if ((strcmp (k, "features.limit-usage") == 0) ||
                    (strcmp (k, "features.soft-limit") == 0))
                        return 0;
        }

        /* snap-max-hard-limit and snap-max-soft-limit are system   *
         * options set and managed by snapshot config option. Hence *
         * they should not be displayed in gluster volume info.     *
         */
        if ((strcmp (k, "snap-max-hard-limit") == 0) ||
            (strcmp (k, "snap-max-soft-limit") == 0))
                return 0;

        snprintf (reconfig_key, 256, "volume%d.option.%s",
                  pack->vol_count, k);
        ret = dict_set_str (pack->dict, reconfig_key, v->data);
        if (0 == ret)
                pack->opt_count++;

        return 0;
}

int
glusterd_add_tier_volume_detail_to_dict (glusterd_volinfo_t *volinfo,
                                    dict_t  *dict, int count)
{
        int            ret            = -1;
        char           key[256]      = {0,};

        GF_ASSERT (volinfo);
        GF_ASSERT (dict);

        memset (key, 0, sizeof (key));
        snprintf (key, 256, "volume%d.cold_type", count);
        ret = dict_set_int32 (dict, key, volinfo->tier_info.cold_type);
        if (ret)
                goto out;

        memset (key, 0, sizeof (key));
        snprintf (key, 256, "volume%d.cold_brick_count", count);
        ret = dict_set_int32 (dict, key, volinfo->tier_info.cold_brick_count);
        if (ret)
                goto out;

        memset (key, 0, sizeof (key));
        snprintf (key, 256, "volume%d.cold_dist_count", count);
        ret = dict_set_int32 (dict, key,
                              volinfo->tier_info.cold_dist_leaf_count);
        if (ret)
                goto out;

        memset (key, 0, sizeof (key));
        snprintf (key, 256, "volume%d.cold_replica_count", count);
        ret = dict_set_int32 (dict, key,
                              volinfo->tier_info.cold_replica_count);
        if (ret)
                goto out;

        memset (key, 0, sizeof (key));
        snprintf (key, 256, "volume%d.cold_arbiter_count", count);
        ret = dict_set_int32 (dict, key, volinfo->arbiter_count);
        if (ret)
                goto out;

        memset (key, 0, sizeof (key));
        snprintf (key, 256, "volume%d.cold_disperse_count", count);
        ret = dict_set_int32 (dict, key,
                              volinfo->tier_info.cold_disperse_count);
        if (ret)
                goto out;

        memset (key, 0, sizeof (key));
        snprintf (key, 256, "volume%d.cold_redundancy_count", count);
        ret = dict_set_int32 (dict, key,
                              volinfo->tier_info.cold_redundancy_count);
        if (ret)
                goto out;

        memset (key, 0, sizeof (key));
        snprintf (key, 256, "volume%d.hot_type", count);
        ret = dict_set_int32 (dict, key, volinfo->tier_info.hot_type);
        if (ret)
                goto out;

        memset (key, 0, sizeof (key));
        snprintf (key, 256, "volume%d.hot_brick_count", count);
        ret = dict_set_int32 (dict, key, volinfo->tier_info.hot_brick_count);
        if (ret)
                goto out;

        memset (key, 0, sizeof (key));
        snprintf (key, 256, "volume%d.hot_replica_count", count);
        ret = dict_set_int32 (dict, key, volinfo->tier_info.hot_replica_count);
        if (ret)
                goto out;

out:
        return ret;

}

int
glusterd_add_arbiter_info_to_bricks (glusterd_volinfo_t *volinfo,
                                     dict_t *volumes, int count)
{
        char                    key[256]    = {0, };
        int                     i           = 0;
        int                     start_index = 0;
        int                     ret         = 0;

        if (volinfo->type == GF_CLUSTER_TYPE_TIER) {
                /*TODO: Add info for hot tier once attach tier of arbiter
                 * volumes is supported. */

                /* cold tier */
                if (volinfo->tier_info.cold_replica_count == 1 ||
                    volinfo->arbiter_count != 1)
                        return 0;

                i = start_index = volinfo->tier_info.hot_brick_count + 1;
                for (; i <= volinfo->brick_count; i++) {
                        if ((i - start_index + 1) %
                            volinfo->tier_info.cold_replica_count != 0)
                                continue;
                        memset (key, 0, sizeof (key));
                        snprintf (key, 256, "volume%d.brick%d.isArbiter",
                                  count, i);
                        ret = dict_set_int32 (volumes, key, 1);
                        if (ret)
                                return ret;
                }
        } else {
                if (volinfo->replica_count == 1 || volinfo->arbiter_count != 1)
                        return 0;
                for (i = 1; i <= volinfo->brick_count; i++) {
                        if (i % volinfo->replica_count != 0)
                                continue;
                        memset (key, 0, sizeof (key));
                        snprintf (key, 256, "volume%d.brick%d.isArbiter",
                                  count, i);
                        ret = dict_set_int32 (volumes, key, 1);
                        if (ret)
                                return ret;
                }
        }
        return 0;
}

int
glusterd_add_volume_detail_to_dict (glusterd_volinfo_t *volinfo,
                                    dict_t  *volumes, int count)
{

        int                     ret = -1;
        char                    key[256] = {0, };
        glusterd_brickinfo_t    *brickinfo = NULL;
        char                    *buf = NULL;
        int                     i = 1;
        dict_t                  *dict = NULL;
        glusterd_conf_t         *priv = NULL;
        char                    *volume_id_str  = NULL;
        struct args_pack        pack = {0,};
        xlator_t                *this = NULL;
        GF_UNUSED int           caps = 0;

        GF_ASSERT (volinfo);
        GF_ASSERT (volumes);

        this = THIS;
        priv = this->private;

        GF_ASSERT (priv);

        snprintf (key, 256, "volume%d.name", count);
        ret = dict_set_str (volumes, key, volinfo->volname);
        if (ret)
                goto out;

        snprintf (key, 256, "volume%d.type", count);
        ret = dict_set_int32 (volumes, key, volinfo->type);
        if (ret)
                goto out;

        snprintf (key, 256, "volume%d.status", count);
        ret = dict_set_int32 (volumes, key, volinfo->status);
        if (ret)
                goto out;

        snprintf (key, 256, "volume%d.brick_count", count);
        ret = dict_set_int32 (volumes, key, volinfo->brick_count);
        if (ret)
                goto out;

        snprintf (key, 256, "volume%d.hot_brick_count", count);
        ret = dict_set_int32 (volumes, key, volinfo->tier_info.hot_brick_count);
        if (ret)
                goto out;

        if (volinfo->type == GF_CLUSTER_TYPE_TIER) {
                ret = glusterd_add_tier_volume_detail_to_dict (volinfo,
                                                       volumes, count);
                if (ret)
                        goto out;
        }

        snprintf (key, 256, "volume%d.dist_count", count);
        ret = dict_set_int32 (volumes, key, volinfo->dist_leaf_count);
        if (ret)
                goto out;

        snprintf (key, 256, "volume%d.stripe_count", count);
        ret = dict_set_int32 (volumes, key, volinfo->stripe_count);
        if (ret)
                goto out;

        snprintf (key, 256, "volume%d.replica_count", count);
        ret = dict_set_int32 (volumes, key, volinfo->replica_count);
        if (ret)
                goto out;

        snprintf (key, 256, "volume%d.disperse_count", count);
        ret = dict_set_int32 (volumes, key, volinfo->disperse_count);
        if (ret)
                goto out;

        snprintf (key, 256, "volume%d.redundancy_count", count);
        ret = dict_set_int32 (volumes, key, volinfo->redundancy_count);
        if (ret)
                goto out;

        snprintf (key, sizeof (key), "volume%d.arbiter_count", count);
        ret = dict_set_int32 (volumes, key, volinfo->arbiter_count);
        if (ret)
                goto out;

        snprintf (key, 256, "volume%d.transport", count);
        ret = dict_set_int32 (volumes, key, volinfo->transport_type);
        if (ret)
                goto out;

        volume_id_str = gf_strdup (uuid_utoa (volinfo->volume_id));
        if (!volume_id_str)
                goto out;

        snprintf (key, sizeof (key), "volume%d.volume_id", count);
        ret = dict_set_dynstr (volumes, key, volume_id_str);
        if (ret)
                goto out;

        snprintf (key, 256, "volume%d.rebalance", count);
        ret = dict_set_int32 (volumes, key, volinfo->rebal.defrag_cmd);
        if (ret)
                goto out;

        snprintf (key, 256, "volume%d.snap_count", count);
        ret = dict_set_int32 (volumes, key, volinfo->snap_count);
        if (ret)
                goto out;

#ifdef HAVE_BD_XLATOR
        if (volinfo->caps) {
                caps = 0;
                snprintf (key, 256, "volume%d.xlator0", count);
                buf = GF_MALLOC (256, gf_common_mt_char);
                if (!buf) {
                        ret = ENOMEM;
                        goto out;
                }
                if (volinfo->caps & CAPS_BD)
                        snprintf (buf, 256, "BD");
                ret = dict_set_dynstr (volumes, key, buf);
                if (ret) {
                        GF_FREE (buf);
                        goto out;
                }

                if (volinfo->caps & CAPS_THIN) {
                        snprintf (key, 256, "volume%d.xlator0.caps%d", count,
                                  caps++);
                        buf = GF_MALLOC (256, gf_common_mt_char);
                        if (!buf) {
                                ret = ENOMEM;
                                goto out;
                        }
                        snprintf (buf, 256, "thin");
                        ret = dict_set_dynstr (volumes, key, buf);
                        if (ret) {
                                GF_FREE (buf);
                                goto out;
                        }
                }

                if (volinfo->caps & CAPS_OFFLOAD_COPY) {
                        snprintf (key, 256, "volume%d.xlator0.caps%d", count,
                                  caps++);
                        buf = GF_MALLOC (256, gf_common_mt_char);
                        if (!buf) {
                                ret = ENOMEM;
                                goto out;
                        }
                        snprintf (buf, 256, "offload_copy");
                        ret = dict_set_dynstr (volumes, key, buf);
                        if (ret) {
                                GF_FREE (buf);
                                goto out;
                        }
                }

                if (volinfo->caps & CAPS_OFFLOAD_SNAPSHOT) {
                        snprintf (key, 256, "volume%d.xlator0.caps%d", count,
                                  caps++);
                        buf = GF_MALLOC (256, gf_common_mt_char);
                        if (!buf) {
                                ret = ENOMEM;
                                goto out;
                        }
                        snprintf (buf, 256, "offload_snapshot");
                        ret = dict_set_dynstr (volumes, key, buf);
                        if (ret)  {
                                GF_FREE (buf);
                                goto out;
                        }
                }

                if (volinfo->caps & CAPS_OFFLOAD_ZERO) {
                        snprintf (key, 256, "volume%d.xlator0.caps%d", count,
                                  caps++);
                        buf = GF_MALLOC (256, gf_common_mt_char);
                        if (!buf) {
                                ret = ENOMEM;
                                goto out;
                        }
                        snprintf (buf, 256, "offload_zerofill");
                        ret = dict_set_dynstr (volumes, key, buf);
                        if (ret)  {
                                GF_FREE (buf);
                                goto out;
                        }
                }

        }
#endif

        cds_list_for_each_entry (brickinfo, &volinfo->bricks, brick_list) {
                char    brick[1024] = {0,};
                char    brick_uuid[64] = {0,};
                snprintf (key, 256, "volume%d.brick%d", count, i);
                snprintf (brick, 1024, "%s:%s", brickinfo->hostname,
                          brickinfo->path);
                buf = gf_strdup (brick);
                ret = dict_set_dynstr (volumes, key, buf);
                if (ret)
                        goto out;
                snprintf (key, 256, "volume%d.brick%d.uuid", count, i);
                snprintf (brick_uuid, 64, "%s", uuid_utoa (brickinfo->uuid));
                buf = gf_strdup (brick_uuid);
                if (!buf)
                        goto out;
                ret = dict_set_dynstr (volumes, key, buf);
                if (ret)
                        goto out;

#ifdef HAVE_BD_XLATOR
                if (volinfo->caps & CAPS_BD) {
                        snprintf (key, 256, "volume%d.vg%d", count, i);
                        snprintf (brick, 1024, "%s", brickinfo->vg);
                        buf = gf_strdup (brick);
                        ret = dict_set_dynstr (volumes, key, buf);
                        if (ret)
                                goto out;
                }
#endif
                i++;
        }
        ret = glusterd_add_arbiter_info_to_bricks (volinfo, volumes, count);
        if (ret)
                goto out;

        dict = volinfo->dict;
        if (!dict) {
                ret = 0;
                goto out;
        }

        pack.dict = volumes;
        pack.vol_count = count;
        pack.opt_count = 0;
        dict_foreach (dict, _build_option_key, (void *) &pack);
        dict_foreach (priv->opts, _build_option_key, &pack);

        snprintf (key, 256, "volume%d.opt_count", pack.vol_count);
        ret = dict_set_int32 (volumes, key, pack.opt_count);
out:
        return ret;
}

int32_t
glusterd_op_txn_begin (rpcsvc_request_t *req, glusterd_op_t op, void *ctx,
                       char *err_str, size_t err_len)
{
        int32_t                     ret             = -1;
        dict_t                     *dict            = NULL;
        xlator_t                   *this            = NULL;
        glusterd_conf_t            *priv            = NULL;
        int32_t                     locked          = 0;
        char                       *tmp             = NULL;
        char                       *volname         = NULL;
        uuid_t                     *txn_id          = NULL;
        glusterd_op_info_t          txn_op_info     = {{0},};
        glusterd_op_sm_event_type_t event_type      = GD_OP_EVENT_NONE;
        uint32_t                    op_errno        = 0;

        GF_ASSERT (req);
        GF_ASSERT ((op > GD_OP_NONE) && (op < GD_OP_MAX));
        GF_ASSERT (NULL != ctx);

        this = THIS;
        GF_ASSERT (this);
        priv = this->private;
        GF_ASSERT (priv);

        dict = ctx;

        /* Generate a transaction-id for this operation and
         * save it in the dict. This transaction id distinguishes
         * each transaction, and helps separate opinfos in the
         * op state machine. */
        ret = glusterd_generate_txn_id (dict, &txn_id);
        if (ret) {
                gf_msg (this->name, GF_LOG_ERROR, 0,
                        GD_MSG_TRANS_IDGEN_FAIL,
                        "Failed to generate transaction id");
                goto out;
        }

        /* Save the MY_UUID as the originator_uuid. This originator_uuid
         * will be used by is_origin_glusterd() to determine if a node
         * is the originator node for a command. */
        ret = glusterd_set_originator_uuid (dict);
        if (ret) {
                gf_msg (this->name, GF_LOG_ERROR, 0,
                        GD_MSG_UUID_SET_FAIL,
                        "Failed to set originator_uuid.");
                goto out;
        }

        /* Based on the op_version, acquire a cluster or mgmt_v3 lock */
        if (priv->op_version < GD_OP_VERSION_3_6_0) {
                ret = glusterd_lock (MY_UUID);
                if (ret) {
                        gf_msg (this->name, GF_LOG_ERROR, 0,
                                GD_MSG_GLUSTERD_LOCK_FAIL,
                                "Unable to acquire lock on localhost, ret: %d",
                                ret);
                        snprintf (err_str, err_len,
                                  "Another transaction is in progress. "
                                  "Please try again after sometime.");
                        goto out;
                }
        } else {
                /* If no volname is given as a part of the command, locks will
                 * not be held */
                ret = dict_get_str (dict, "volname", &tmp);
                if (ret) {
                        gf_msg (this->name, GF_LOG_INFO, errno,
                                GD_MSG_DICT_GET_FAILED,
                                "No Volume name present. "
                                "Locks not being held.");
                        goto local_locking_done;
                } else {
                        /* Use a copy of volname, as cli response will be
                         * sent before the unlock, and the volname in the
                         * dict, might be removed */
                        volname = gf_strdup (tmp);
                        if (!volname)
                                goto out;
                }

                ret = glusterd_mgmt_v3_lock (volname, MY_UUID, &op_errno,
                                             "vol");
                if (ret) {
                        gf_msg (this->name, GF_LOG_ERROR, 0,
                                GD_MSG_MGMTV3_LOCK_GET_FAIL,
                                "Unable to acquire lock for %s", volname);
                        snprintf (err_str, err_len,
                                  "Another transaction is in progress for %s. "
                                  "Please try again after sometime.", volname);
                        goto out;
                }
        }

        locked = 1;
        gf_msg_debug (this->name, 0, "Acquired lock on localhost");

local_locking_done:
        /* If no volname is given as a part of the command, locks will
         * not be held, hence sending stage event. */
        if (volname || (priv->op_version < GD_OP_VERSION_3_6_0))
                event_type = GD_OP_EVENT_START_LOCK;
        else {
                txn_op_info.state.state = GD_OP_STATE_LOCK_SENT;
                event_type = GD_OP_EVENT_ALL_ACC;
        }

        /* Save opinfo for this transaction with the transaction id */
        glusterd_txn_opinfo_init (&txn_op_info, NULL, &op, ctx, req);

        ret = glusterd_set_txn_opinfo (txn_id, &txn_op_info);
        if (ret) {
                gf_msg (this->name, GF_LOG_ERROR, 0,
                        GD_MSG_TRANS_OPINFO_SET_FAIL,
                        "Unable to set transaction's opinfo");
                if (ctx)
                        dict_unref (ctx);
                goto out;
        }

        ret = glusterd_op_sm_inject_event (event_type, txn_id, ctx);
        if (ret) {
                gf_msg (this->name, GF_LOG_ERROR, 0,
                        GD_MSG_EVENT_INJECT_FAIL, "Failed to acquire cluster"
                        " lock.");
                goto out;
        }

out:
        if (locked && ret) {
                /* Based on the op-version, we release the
                 * cluster or mgmt_v3 lock */
                if (priv->op_version < GD_OP_VERSION_3_6_0)
                        glusterd_unlock (MY_UUID);
                else {
                        ret = glusterd_mgmt_v3_unlock (volname, MY_UUID,
                                                       "vol");
                        if (ret)
                                gf_msg (this->name, GF_LOG_ERROR, 0,
                                        GD_MSG_MGMTV3_UNLOCK_FAIL,
                                        "Unable to release lock for %s",
                                        volname);
                        ret = -1;
                }
        }

        if (volname)
                GF_FREE (volname);

        gf_msg_debug (this->name, 0, "Returning %d", ret);
        return ret;
}

int
__glusterd_handle_cluster_lock (rpcsvc_request_t *req)
{
        dict_t                         *op_ctx      = NULL;
        int32_t                         ret         = -1;
        gd1_mgmt_cluster_lock_req       lock_req    = {{0},};
        glusterd_op_lock_ctx_t         *ctx         = NULL;
        glusterd_op_sm_event_type_t     op          = GD_OP_EVENT_LOCK;
        glusterd_op_info_t              txn_op_info = {{0},};
        glusterd_conf_t                *priv        = NULL;
        uuid_t                         *txn_id      = NULL;
        xlator_t                       *this        = NULL;

        this = THIS;
        GF_ASSERT (this);
        priv = this->private;
        GF_ASSERT (priv);
        GF_ASSERT (req);

        txn_id = &priv->global_txn_id;

        ret = xdr_to_generic (req->msg[0], &lock_req,
                              (xdrproc_t)xdr_gd1_mgmt_cluster_lock_req);
        if (ret < 0) {
                gf_msg (this->name, GF_LOG_ERROR, 0,
                        GD_MSG_REQ_DECODE_FAIL, "Failed to decode lock "
                        "request received from peer");
                req->rpc_err = GARBAGE_ARGS;
                goto out;
        }

        gf_msg_debug (this->name, 0, "Received LOCK from uuid: %s",
                uuid_utoa (lock_req.uuid));

        rcu_read_lock ();
        ret = (glusterd_peerinfo_find_by_uuid (lock_req.uuid) == NULL);
        rcu_read_unlock ();
        if (ret) {
                gf_msg (this->name, GF_LOG_WARNING, 0,
                        GD_MSG_PEER_NOT_FOUND, "%s doesn't "
                        "belong to the cluster. Ignoring request.",
                        uuid_utoa (lock_req.uuid));
                ret = -1;
                goto out;
        }

        ctx = GF_CALLOC (1, sizeof (*ctx), gf_gld_mt_op_lock_ctx_t);

        if (!ctx) {
                //respond here
                return -1;
        }

        gf_uuid_copy (ctx->uuid, lock_req.uuid);
        ctx->req = req;
        ctx->dict = NULL;

        op_ctx =  dict_new ();
        if (!op_ctx) {
                gf_msg (this->name, GF_LOG_ERROR, ENOMEM,
                        GD_MSG_DICT_CREATE_FAIL,
                        "Unable to set new dict");
                goto out;
        }

        glusterd_txn_opinfo_init (&txn_op_info, NULL, &op, op_ctx, req);

        ret = glusterd_set_txn_opinfo (txn_id, &txn_op_info);
        if (ret) {
                gf_msg (this->name, GF_LOG_ERROR, 0,
                        GD_MSG_TRANS_OPINFO_SET_FAIL,
                        "Unable to set transaction's opinfo");
                dict_unref (txn_op_info.op_ctx);
                goto out;
        }

        ret = glusterd_op_sm_inject_event (GD_OP_EVENT_LOCK, txn_id, ctx);
        if (ret)
                gf_msg (this->name, GF_LOG_ERROR, 0,
                        GD_MSG_EVENT_INJECT_FAIL,
                        "Failed to inject event GD_OP_EVENT_LOCK");

out:
        gf_msg_debug (this->name, 0, "Returning %d", ret);

        glusterd_friend_sm ();
        glusterd_op_sm ();

        return ret;
}

int
glusterd_handle_cluster_lock (rpcsvc_request_t *req)
{
        return glusterd_big_locked_handler (req,
                                            __glusterd_handle_cluster_lock);
}

static int
glusterd_req_ctx_create (rpcsvc_request_t *rpc_req,
                         int op, uuid_t uuid,
                         char *buf_val, size_t buf_len,
                         gf_gld_mem_types_t mem_type,
                         glusterd_req_ctx_t **req_ctx_out)
{
        int                 ret     = -1;
        char                str[50] = {0,};
        glusterd_req_ctx_t *req_ctx = NULL;
        dict_t             *dict    = NULL;
        xlator_t           *this    = NULL;

        this = THIS;
        GF_ASSERT (this);

        gf_uuid_unparse (uuid, str);
        gf_msg_debug (this->name, 0, "Received op from uuid %s", str);

        dict = dict_new ();
        if (!dict)
                goto out;

        req_ctx = GF_CALLOC (1, sizeof (*req_ctx), mem_type);
        if (!req_ctx) {
                goto out;
        }

        gf_uuid_copy (req_ctx->uuid, uuid);
        req_ctx->op = op;
        ret = dict_unserialize (buf_val, buf_len, &dict);
        if (ret) {
                gf_msg (this->name, GF_LOG_WARNING, 0,
                        GD_MSG_DICT_UNSERIALIZE_FAIL,
                        "failed to unserialize the dictionary");
                goto out;
        }

        req_ctx->dict = dict;
        req_ctx->req = rpc_req;
        *req_ctx_out = req_ctx;
        ret = 0;
out:
        if (ret) {
                if (dict)
                        dict_unref (dict);
                GF_FREE (req_ctx);
        }
        return ret;
}

int
__glusterd_handle_stage_op (rpcsvc_request_t *req)
{
        int32_t                         ret = -1;
        glusterd_req_ctx_t              *req_ctx = NULL;
        gd1_mgmt_stage_op_req           op_req = {{0},};
        xlator_t                        *this = NULL;
        uuid_t                          *txn_id = NULL;
        glusterd_op_info_t              txn_op_info = {{0},};
        glusterd_op_sm_state_info_t     state = {0,};
        glusterd_conf_t                 *priv = NULL;

        this = THIS;
        GF_ASSERT (this);
        priv = this->private;
        GF_ASSERT (priv);
        GF_ASSERT (req);

        txn_id = &priv->global_txn_id;

        ret = xdr_to_generic (req->msg[0], &op_req,
                              (xdrproc_t)xdr_gd1_mgmt_stage_op_req);
        if (ret < 0) {
                gf_msg (this->name, GF_LOG_ERROR, 0,
                        GD_MSG_REQ_DECODE_FAIL, "Failed to decode stage "
                        "request received from peer");
                req->rpc_err = GARBAGE_ARGS;
                goto out;
        }

        ret = glusterd_req_ctx_create (req, op_req.op, op_req.uuid,
                                       op_req.buf.buf_val, op_req.buf.buf_len,
                                       gf_gld_mt_op_stage_ctx_t, &req_ctx);
        if (ret) {
                gf_msg (this->name, GF_LOG_ERROR, 0,
                        GD_MSG_REQ_CTX_CREATE_FAIL, "Failed to create req_ctx");
                goto out;
        }

        ret = dict_get_bin (req_ctx->dict, "transaction_id", (void **)&txn_id);
        gf_msg_debug (this->name, 0, "transaction ID = %s",
                uuid_utoa (*txn_id));

        rcu_read_lock ();
        ret = (glusterd_peerinfo_find_by_uuid (op_req.uuid) == NULL);
        rcu_read_unlock ();
        if (ret) {
                gf_msg (this->name, GF_LOG_WARNING, 0,
                        GD_MSG_PEER_NOT_FOUND, "%s doesn't "
                        "belong to the cluster. Ignoring request.",
                        uuid_utoa (op_req.uuid));
                ret = -1;
                goto out;
        }

        /* In cases where there is no volname, the receivers won't have a
         * transaction opinfo created, as for those operations, the locking
         * phase where the transaction opinfos are created, won't be called. */
        ret = glusterd_get_txn_opinfo (txn_id, &txn_op_info);
        if (ret) {
                gf_msg_debug (this->name, 0,
                        "No transaction's opinfo set");

                state.state = GD_OP_STATE_LOCKED;
                glusterd_txn_opinfo_init (&txn_op_info, &state, &op_req.op,
                                          req_ctx->dict, req);

                ret = glusterd_set_txn_opinfo (txn_id, &txn_op_info);
                if (ret) {
                        gf_msg (this->name, GF_LOG_ERROR, 0,
                                GD_MSG_TRANS_OPINFO_SET_FAIL,
                                "Unable to set transaction's opinfo");
                        dict_unref (req_ctx->dict);
                        goto out;
                }
        }

        ret = glusterd_op_sm_inject_event (GD_OP_EVENT_STAGE_OP,
                                           txn_id, req_ctx);
        if (ret)
                gf_msg (this->name, GF_LOG_ERROR, 0,
                        GD_MSG_EVENT_INJECT_FAIL,
                        "Failed to inject event GD_OP_EVENT_STAGE_OP");

 out:
        free (op_req.buf.buf_val);//malloced by xdr
        glusterd_friend_sm ();
        glusterd_op_sm ();
        return ret;
}

int
glusterd_handle_stage_op (rpcsvc_request_t *req)
{
        return glusterd_big_locked_handler (req, __glusterd_handle_stage_op);
}


int
__glusterd_handle_commit_op (rpcsvc_request_t *req)
{
        int32_t                         ret = -1;
        glusterd_req_ctx_t              *req_ctx = NULL;
        gd1_mgmt_commit_op_req          op_req = {{0},};
        xlator_t                        *this = NULL;
        uuid_t                          *txn_id = NULL;
        glusterd_conf_t                 *priv = NULL;

        this = THIS;
        GF_ASSERT (this);
        priv = this->private;
        GF_ASSERT (priv);
        GF_ASSERT (req);

        txn_id = &priv->global_txn_id;

        ret = xdr_to_generic (req->msg[0], &op_req,
                              (xdrproc_t)xdr_gd1_mgmt_commit_op_req);
        if (ret < 0) {
                gf_msg (this->name, GF_LOG_ERROR, 0,
                        GD_MSG_REQ_DECODE_FAIL, "Failed to decode commit "
                        "request received from peer");
                req->rpc_err = GARBAGE_ARGS;
                goto out;
        }

        rcu_read_lock ();
        ret = (glusterd_peerinfo_find_by_uuid (op_req.uuid) == NULL);
        rcu_read_unlock ();
        if (ret) {
                gf_msg (this->name, GF_LOG_WARNING, 0,
                        GD_MSG_PEER_NOT_FOUND, "%s doesn't "
                        "belong to the cluster. Ignoring request.",
                        uuid_utoa (op_req.uuid));
                ret = -1;
                goto out;
        }

        //the structures should always be equal
        GF_ASSERT (sizeof (gd1_mgmt_commit_op_req) == sizeof (gd1_mgmt_stage_op_req));
        ret = glusterd_req_ctx_create (req, op_req.op, op_req.uuid,
                                       op_req.buf.buf_val, op_req.buf.buf_len,
                                       gf_gld_mt_op_commit_ctx_t, &req_ctx);
        if (ret)
                goto out;

        ret = dict_get_bin (req_ctx->dict, "transaction_id", (void **)&txn_id);
        gf_msg_debug (this->name, 0, "transaction ID = %s",
                uuid_utoa (*txn_id));

        ret = glusterd_op_sm_inject_event (GD_OP_EVENT_COMMIT_OP,
                                           txn_id, req_ctx);

out:
        free (op_req.buf.buf_val);//malloced by xdr
        glusterd_friend_sm ();
        glusterd_op_sm ();
        return ret;
}

int
glusterd_handle_commit_op (rpcsvc_request_t *req)
{
        return glusterd_big_locked_handler (req, __glusterd_handle_commit_op);
}

int
__glusterd_handle_cli_probe (rpcsvc_request_t *req)
{
        int32_t                         ret = -1;
        gf_cli_req                  cli_req = {{0,},};
        glusterd_peerinfo_t       *peerinfo = NULL;
        gf_boolean_t                run_fsm = _gf_true;
        xlator_t                      *this = NULL;
        char                     *bind_name = NULL;
        dict_t                        *dict = NULL;
        char                      *hostname = NULL;
        int                            port = 0;
        int                        op_errno = 0;

        GF_ASSERT (req);
        this = THIS;

        ret = xdr_to_generic (req->msg[0], &cli_req, (xdrproc_t)xdr_gf_cli_req);
        if (ret < 0)  {
                //failed to decode msg;
                gf_msg (this->name, GF_LOG_ERROR, 0,
                        GD_MSG_REQ_DECODE_FAIL, "xdr decoding error");
                req->rpc_err = GARBAGE_ARGS;
                goto out;
        }

        if (cli_req.dict.dict_len) {
                dict = dict_new ();

                ret = dict_unserialize (cli_req.dict.dict_val,
                                        cli_req.dict.dict_len, &dict);
                if (ret < 0) {
                        gf_msg (this->name, GF_LOG_ERROR, 0,
                                GD_MSG_DICT_UNSERIALIZE_FAIL, "Failed to "
                                "unserialize req-buffer to dictionary");
                        goto out;
                }
        }

        ret = dict_get_str (dict, "hostname", &hostname);
        if (ret) {
                gf_msg (this->name, GF_LOG_ERROR, 0,
                        GD_MSG_HOSTNAME_NOTFOUND_IN_DICT,
                        "Failed to get hostname");
                goto out;
        }

        ret = dict_get_int32 (dict, "port", &port);
        if (ret) {
                gf_msg (this->name, GF_LOG_ERROR, 0,
                        GD_MSG_PORT_NOTFOUND_IN_DICT, "Failed to get port");
                goto out;
        }

        if (glusterd_is_any_volume_in_server_quorum (this) &&
            !does_gd_meet_server_quorum (this)) {
                glusterd_xfer_cli_probe_resp (req, -1, GF_PROBE_QUORUM_NOT_MET,
                                              NULL, hostname, port, dict);
                gf_msg (this->name, GF_LOG_CRITICAL, 0,
                        GD_MSG_SERVER_QUORUM_NOT_MET,
                        "Server quorum not met. Rejecting operation.");
                ret = 0;
                goto out;
        }

        gf_msg ("glusterd", GF_LOG_INFO, 0,
                GD_MSG_CLI_REQ_RECVD,
                "Received CLI probe req %s %d",
                hostname, port);

        if (dict_get_str(this->options,"transport.socket.bind-address",
                         &bind_name) == 0) {
                gf_msg_debug ("glusterd", 0,
                        "only checking probe address vs. bind address");
                ret = gf_is_same_address (bind_name, hostname);
        }
        else {
                ret = gf_is_local_addr (hostname);
        }
        if (ret) {
                glusterd_xfer_cli_probe_resp (req, 0, GF_PROBE_LOCALHOST,
                                              NULL, hostname, port, dict);
                ret = 0;
                goto out;
        }

        rcu_read_lock ();

        peerinfo = glusterd_peerinfo_find_by_hostname (hostname);
        ret = (peerinfo && gd_peer_has_address (peerinfo, hostname));

        rcu_read_unlock ();

        if (ret) {
                gf_msg_debug ("glusterd", 0, "Probe host %s port %d "
                        "already a peer", hostname, port);
                glusterd_xfer_cli_probe_resp (req, 0, GF_PROBE_FRIEND, NULL,
                                              hostname, port, dict);
                ret = 0;
                goto out;
        }

        ret = glusterd_probe_begin (req, hostname, port, dict, &op_errno);

        if (ret == GLUSTERD_CONNECTION_AWAITED) {
                //fsm should be run after connection establishes
                run_fsm = _gf_false;
                ret = 0;

        } else if (ret == -1) {
                glusterd_xfer_cli_probe_resp (req, -1, op_errno,
                                              NULL, hostname, port, dict);
                goto out;
        }

out:
        free (cli_req.dict.dict_val);

        if (run_fsm) {
                glusterd_friend_sm ();
                glusterd_op_sm ();
        }

        return ret;
}

int
glusterd_handle_cli_probe (rpcsvc_request_t *req)
{
        return glusterd_big_locked_handler (req, __glusterd_handle_cli_probe);
}

int
__glusterd_handle_cli_deprobe (rpcsvc_request_t *req)
{
        int32_t                         ret = -1;
        gf_cli_req                  cli_req = {{0,},};
        uuid_t                         uuid = {0};
        int                        op_errno = 0;
        xlator_t                      *this = NULL;
        glusterd_conf_t               *priv = NULL;
        dict_t                        *dict = NULL;
        char                      *hostname = NULL;
        int                            port = 0;
        int                           flags = 0;
        glusterd_volinfo_t         *volinfo = NULL;
        glusterd_volinfo_t             *tmp = NULL;

        this = THIS;
        GF_ASSERT (this);
        priv = this->private;
        GF_ASSERT (priv);
        GF_ASSERT (req);

        ret = xdr_to_generic (req->msg[0], &cli_req,
                              (xdrproc_t)xdr_gf_cli_req);
        if (ret < 0) {
                //failed to decode msg;
                gf_msg (this->name, GF_LOG_ERROR, 0,
                        GD_MSG_REQ_DECODE_FAIL, "Failed to decode "
                        "request received from cli");
                req->rpc_err = GARBAGE_ARGS;
                goto out;
        }

        if (cli_req.dict.dict_len) {
                dict = dict_new ();

                ret = dict_unserialize (cli_req.dict.dict_val,
                                        cli_req.dict.dict_len, &dict);
                if (ret < 0) {
                        gf_msg (this->name, GF_LOG_ERROR, 0,
                                GD_MSG_DICT_UNSERIALIZE_FAIL, "Failed to "
                                "unserialize req-buffer to dictionary");
                        goto out;
                }
        }

        gf_msg ("glusterd", GF_LOG_INFO, 0,
                GD_MSG_CLI_REQ_RECVD,
                "Received CLI deprobe req");

        ret = dict_get_str (dict, "hostname", &hostname);
        if (ret) {
                gf_msg (this->name, GF_LOG_ERROR, 0,
                        GD_MSG_HOSTNAME_NOTFOUND_IN_DICT,
                        "Failed to get hostname");
                goto out;
        }

        ret = dict_get_int32 (dict, "port", &port);
        if (ret) {
                gf_msg (this->name, GF_LOG_ERROR, 0,
                        GD_MSG_PORT_NOTFOUND_IN_DICT, "Failed to get port");
                goto out;
        }
        ret = dict_get_int32 (dict, "flags", &flags);
        if (ret) {
                gf_msg (this->name, GF_LOG_ERROR, 0,
                        GD_MSG_FLAGS_NOTFOUND_IN_DICT, "Failed to get flags");
                goto out;
        }

        ret = glusterd_hostname_to_uuid (hostname, uuid);
        if (ret) {
                op_errno = GF_DEPROBE_NOT_FRIEND;
                goto out;
        }

        if (!gf_uuid_compare (uuid, MY_UUID)) {
                op_errno = GF_DEPROBE_LOCALHOST;
                ret = -1;
                goto out;
        }

        if (!(flags & GF_CLI_FLAG_OP_FORCE)) {
                /* Check if peers are connected, except peer being
                * detached*/
                if (!glusterd_chk_peers_connected_befriended (uuid)) {
                        ret = -1;
                        op_errno = GF_DEPROBE_FRIEND_DOWN;
                        goto out;
                }
        }

        /* Check for if volumes exist with some bricks on the peer being
        * detached. It's not a problem if a volume contains none or all
        * of its bricks on the peer being detached
        */
        cds_list_for_each_entry_safe (volinfo, tmp, &priv->volumes,
                                      vol_list) {
                ret = glusterd_friend_contains_vol_bricks (volinfo,
                                                           uuid);
                if (ret == 1) {
                        op_errno = GF_DEPROBE_BRICK_EXIST;
                        goto out;
                }
        }

        if (!(flags & GF_CLI_FLAG_OP_FORCE)) {
                if (glusterd_is_any_volume_in_server_quorum (this) &&
                    !does_gd_meet_server_quorum (this)) {
                        gf_msg (this->name, GF_LOG_CRITICAL, 0,
                                GD_MSG_SERVER_QUORUM_NOT_MET,
                                "Server quorum not met. Rejecting operation.");
                        ret = -1;
                        op_errno = GF_DEPROBE_QUORUM_NOT_MET;
                        goto out;
                }
        }

        if (!gf_uuid_is_null (uuid)) {
                ret = glusterd_deprobe_begin (req, hostname, port, uuid, dict,
                                              &op_errno);
        } else {
                ret = glusterd_deprobe_begin (req, hostname, port, NULL, dict,
                                              &op_errno);
        }

out:
        free (cli_req.dict.dict_val);

        if (ret) {
                ret = glusterd_xfer_cli_deprobe_resp (req, ret, op_errno, NULL,
                                                      hostname, dict);
        }

        glusterd_friend_sm ();
        glusterd_op_sm ();

        return ret;
}

int
glusterd_handle_cli_deprobe (rpcsvc_request_t *req)
{
        return glusterd_big_locked_handler (req, __glusterd_handle_cli_deprobe);
}

int
__glusterd_handle_cli_list_friends (rpcsvc_request_t *req)
{
        int32_t                         ret = -1;
        gf1_cli_peer_list_req           cli_req = {0,};
        dict_t                          *dict = NULL;

        GF_ASSERT (req);

        ret = xdr_to_generic (req->msg[0], &cli_req,
                              (xdrproc_t)xdr_gf1_cli_peer_list_req);
        if (ret < 0) {
                //failed to decode msg;
                gf_msg ("glusterd", GF_LOG_ERROR, 0,
                        GD_MSG_REQ_DECODE_FAIL, "Failed to decode "
                        "request received from cli");
                req->rpc_err = GARBAGE_ARGS;
                goto out;
        }

        gf_msg ("glusterd", GF_LOG_INFO, 0,
                GD_MSG_CLI_REQ_RECVD,
                "Received cli list req");

        if (cli_req.dict.dict_len) {
                /* Unserialize the dictionary */
                dict  = dict_new ();

                ret = dict_unserialize (cli_req.dict.dict_val,
                                        cli_req.dict.dict_len,
                                        &dict);
                if (ret < 0) {
                        gf_msg ("glusterd", GF_LOG_ERROR, 0,
                                GD_MSG_DICT_UNSERIALIZE_FAIL,
                                "failed to "
                                "unserialize req-buffer to dictionary");
                        goto out;
                } else {
                        dict->extra_stdfree = cli_req.dict.dict_val;
                }
        }

        ret = glusterd_list_friends (req, dict, cli_req.flags);

out:
        if (dict)
                dict_unref (dict);

        glusterd_friend_sm ();
        glusterd_op_sm ();

        return ret;
}

int
glusterd_handle_cli_list_friends (rpcsvc_request_t *req)
{
        return glusterd_big_locked_handler (req,
                                            __glusterd_handle_cli_list_friends);
}

static int
__glusterd_handle_cli_get_volume (rpcsvc_request_t *req)
{
        int32_t                         ret = -1;
        gf_cli_req                      cli_req = {{0,}};
        int32_t                         flags = 0;
        dict_t                         *dict = NULL;
        xlator_t                       *this = NULL;

        GF_ASSERT (req);
        this = THIS;

        ret = xdr_to_generic (req->msg[0], &cli_req, (xdrproc_t)xdr_gf_cli_req);
        if (ret < 0) {
                //failed to decode msg;
                gf_msg (this->name, GF_LOG_ERROR, 0,
                        GD_MSG_REQ_DECODE_FAIL, "Failed to decode "
                        "request received from cli");
                req->rpc_err = GARBAGE_ARGS;
                goto out;
        }

        gf_msg (this->name, GF_LOG_INFO, 0,
                GD_MSG_GET_VOL_REQ_RCVD,
                "Received get vol req");

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
                        goto out;
                } else {
                        dict->extra_stdfree = cli_req.dict.dict_val;
                }
        }

        ret = dict_get_int32 (dict, "flags", &flags);
        if (ret) {
                gf_msg (this->name, GF_LOG_ERROR, 0,
                         GD_MSG_FLAGS_NOTFOUND_IN_DICT, "failed to get flags");
                goto out;
        }
        ret = glusterd_get_volumes (req, dict, flags);

out:
        if (dict)
                dict_unref (dict);

        glusterd_friend_sm ();
        glusterd_op_sm ();

        return ret;
}

int
glusterd_handle_cli_get_volume (rpcsvc_request_t *req)
{
        return glusterd_big_locked_handler (req,
                                            __glusterd_handle_cli_get_volume);
}

int
__glusterd_handle_cli_uuid_reset (rpcsvc_request_t *req)
{
        int                     ret     = -1;
        dict_t                  *dict   = NULL;
        xlator_t                *this   = NULL;
        glusterd_conf_t         *priv   = NULL;
        uuid_t                  uuid    = {0};
        gf_cli_rsp              rsp     = {0,};
        gf_cli_req              cli_req = {{0,}};
        char                    msg_str[2048] = {0,};

        GF_ASSERT (req);

        this = THIS;
        priv = this->private;
        GF_ASSERT (priv);

        ret = xdr_to_generic (req->msg[0], &cli_req, (xdrproc_t)xdr_gf_cli_req);
        if (ret < 0) {
                //failed to decode msg;
                gf_msg (this->name, GF_LOG_ERROR, 0,
                        GD_MSG_REQ_DECODE_FAIL, "Failed to decode "
                        "request received from cli");
                req->rpc_err = GARBAGE_ARGS;
                goto out;
        }

        gf_msg_debug ("glusterd", 0, "Received uuid reset req");

        if (cli_req.dict.dict_len) {
                /* Unserialize the dictionary */
                dict  = dict_new ();

                ret = dict_unserialize (cli_req.dict.dict_val,
                                        cli_req.dict.dict_len,
                                        &dict);
                if (ret < 0) {
                        gf_msg ("glusterd", GF_LOG_ERROR, 0,
                                GD_MSG_DICT_UNSERIALIZE_FAIL,
                                "failed to "
                                "unserialize req-buffer to dictionary");
                        snprintf (msg_str, sizeof (msg_str), "Unable to decode "
                                  "the buffer");
                        goto out;
                } else {
                        dict->extra_stdfree = cli_req.dict.dict_val;
                }
        }

        /* In the above section if dict_unserialize is successful, ret is set
         * to zero.
         */
        ret = -1;
        // Do not allow peer reset if there are any volumes in the cluster
        if (!cds_list_empty (&priv->volumes)) {
                snprintf (msg_str, sizeof (msg_str), "volumes are already "
                          "present in the cluster. Resetting uuid is not "
                          "allowed");
                gf_msg (this->name, GF_LOG_WARNING, 0,
                        GD_MSG_VOLS_ALREADY_PRESENT, "%s", msg_str);
                goto out;
        }

        // Do not allow peer reset if trusted storage pool is already formed
        if (!cds_list_empty (&priv->peers)) {
                snprintf (msg_str, sizeof (msg_str),"trusted storage pool "
                          "has been already formed. Please detach this peer "
                          "from the pool and reset its uuid.");
                gf_msg (this->name, GF_LOG_WARNING, 0,
                        GD_MSG_TSP_ALREADY_FORMED, "%s", msg_str);
                goto out;
        }

        gf_uuid_copy (uuid, priv->uuid);
        ret = glusterd_uuid_generate_save ();

        if (!gf_uuid_compare (uuid, MY_UUID)) {
                snprintf (msg_str, sizeof (msg_str), "old uuid and the new uuid"
                          " are same. Try gluster peer reset again");
                gf_msg (this->name, GF_LOG_ERROR, 0,
                        GD_MSG_UUIDS_SAME_RETRY, "%s", msg_str);
                ret = -1;
                goto out;
        }

out:
        if (ret) {
                rsp.op_ret = -1;
                if (msg_str[0] == '\0')
                        snprintf (msg_str, sizeof (msg_str), "Operation "
                                  "failed");
                rsp.op_errstr = msg_str;
                ret = 0;
        } else {
                rsp.op_errstr = "";
        }

        glusterd_to_cli (req, &rsp, NULL, 0, NULL,
                         (xdrproc_t)xdr_gf_cli_rsp, dict);

        return ret;
}

int
glusterd_handle_cli_uuid_reset (rpcsvc_request_t *req)
{
        return glusterd_big_locked_handler (req,
                                            __glusterd_handle_cli_uuid_reset);
}

int
__glusterd_handle_cli_uuid_get (rpcsvc_request_t *req)
{
        int                     ret         = -1;
        dict_t                  *dict       = NULL;
        dict_t                  *rsp_dict   = NULL;
        xlator_t                *this       = NULL;
        glusterd_conf_t         *priv       = NULL;
        gf_cli_rsp              rsp         = {0,};
        gf_cli_req              cli_req     = {{0,}};
        char                    msg_str[2048] = {0,};
        char                    uuid_str[64] = {0,};

        GF_ASSERT (req);

        this = THIS;
        priv = this->private;
        GF_ASSERT (priv);

        ret = xdr_to_generic (req->msg[0], &cli_req, (xdrproc_t)xdr_gf_cli_req);
        if (ret < 0) {
                gf_msg (this->name, GF_LOG_ERROR, 0,
                        GD_MSG_REQ_DECODE_FAIL, "Failed to decode "
                        "request received from cli");
                req->rpc_err = GARBAGE_ARGS;
                goto out;
        }

        gf_msg_debug ("glusterd", 0, "Received uuid get req");

        if (cli_req.dict.dict_len) {
                dict  = dict_new ();
                if (!dict) {
                        ret = -1;
                        goto out;
                }

                ret = dict_unserialize (cli_req.dict.dict_val,
                                        cli_req.dict.dict_len,
                                        &dict);
                if (ret < 0) {
                        gf_msg ("glusterd", GF_LOG_ERROR, 0,
                                GD_MSG_DICT_UNSERIALIZE_FAIL,
                                "failed to "
                                "unserialize req-buffer to dictionary");
                        snprintf (msg_str, sizeof (msg_str), "Unable to decode "
                                  "the buffer");
                        goto out;

                } else {
                        dict->extra_stdfree = cli_req.dict.dict_val;

                }
        }

        rsp_dict = dict_new ();
        if (!rsp_dict) {
                ret = -1;
                goto out;
        }

        uuid_utoa_r (MY_UUID, uuid_str);
        ret = dict_set_str (rsp_dict, "uuid", uuid_str);
        if (ret) {
                gf_msg (this->name, GF_LOG_ERROR, 0,
                        GD_MSG_DICT_SET_FAILED, "Failed to set uuid in "
                        "dictionary.");
                goto out;
        }

        ret = dict_allocate_and_serialize (rsp_dict, &rsp.dict.dict_val,
                                           &rsp.dict.dict_len);
        if (ret) {
                gf_msg (this->name, GF_LOG_ERROR, 0,
                        GD_MSG_DICT_SERL_LENGTH_GET_FAIL,
                        "Failed to serialize "
                        "dictionary.");
                goto out;
        }
        ret = 0;
out:
        if (ret) {
                rsp.op_ret = -1;
                if (msg_str[0] == '\0')
                        snprintf (msg_str, sizeof (msg_str), "Operation "
                                  "failed");
                rsp.op_errstr = msg_str;

        } else {
                rsp.op_errstr = "";

        }

        glusterd_to_cli (req, &rsp, NULL, 0, NULL,
                         (xdrproc_t)xdr_gf_cli_rsp, dict);

        return 0;
}
int
glusterd_handle_cli_uuid_get (rpcsvc_request_t *req)
{
        return glusterd_big_locked_handler (req,
                                            __glusterd_handle_cli_uuid_get);
}

int
__glusterd_handle_cli_list_volume (rpcsvc_request_t *req)
{
        int                     ret = -1;
        dict_t                  *dict = NULL;
        glusterd_conf_t         *priv = NULL;
        glusterd_volinfo_t      *volinfo = NULL;
        int                     count = 0;
        char                    key[1024] = {0,};
        gf_cli_rsp              rsp = {0,};

        GF_ASSERT (req);

        priv = THIS->private;
        GF_ASSERT (priv);

        dict = dict_new ();
        if (!dict)
                goto out;

        cds_list_for_each_entry (volinfo, &priv->volumes, vol_list) {
                memset (key, 0, sizeof (key));
                snprintf (key, sizeof (key), "volume%d", count);
                ret = dict_set_str (dict, key, volinfo->volname);
                if (ret)
                        goto out;
                count++;
        }

        ret = dict_set_int32 (dict, "count", count);
        if (ret)
                goto out;

        ret = dict_allocate_and_serialize (dict, &rsp.dict.dict_val,
                                           &rsp.dict.dict_len);
        if (ret)
                goto out;

        ret = 0;

out:
        rsp.op_ret = ret;
        if (ret)
                rsp.op_errstr = "Error listing volumes";
        else
                rsp.op_errstr = "";

        glusterd_submit_reply (req, &rsp, NULL, 0, NULL,
                                     (xdrproc_t)xdr_gf_cli_rsp);
        ret = 0;

        if (dict)
                dict_unref (dict);

        glusterd_friend_sm ();
        glusterd_op_sm ();

        return ret;
}

int
glusterd_handle_cli_list_volume (rpcsvc_request_t *req)
{
        return glusterd_big_locked_handler (req,
                                            __glusterd_handle_cli_list_volume);
}

int32_t
glusterd_op_begin (rpcsvc_request_t *req, glusterd_op_t op, void *ctx,
                   char *err_str, size_t err_len)
{
        int             ret = -1;

        ret = glusterd_op_txn_begin (req, op, ctx, err_str, err_len);

        return ret;
}

int
__glusterd_handle_ganesha_cmd (rpcsvc_request_t *req)
{
        int32_t                         ret = -1;
        gf_cli_req                      cli_req = { {0,} } ;
        dict_t                          *dict = NULL;
        glusterd_op_t                   cli_op = GD_OP_GANESHA;
        char                            *op_errstr = NULL;
        char                            err_str[2048] = {0,};
        xlator_t                        *this = NULL;

        this = THIS;
        GF_ASSERT (this);

        GF_ASSERT (req);

        ret = xdr_to_generic (req->msg[0], &cli_req, (xdrproc_t)xdr_gf_cli_req);
        if (ret < 0) {
                snprintf (err_str, sizeof (err_str), "Failed to decode "
                          "request received from cli");
                gf_msg (this->name, GF_LOG_ERROR, 0,
                        GD_MSG_REQ_DECODE_FAIL, "%s", err_str);
                req->rpc_err = GARBAGE_ARGS;
                goto out;
        }

        if (cli_req.dict.dict_len) {
                /* Unserialize the dictionary */
                dict  = dict_new ();
                if (!dict) {
                        ret = -1;
                        goto out;
                }

                ret = dict_unserialize (cli_req.dict.dict_val,
                                        cli_req.dict.dict_len,
                                        &dict);
                if (ret < 0) {
                        gf_msg (this->name, GF_LOG_ERROR, 0,
                                GD_MSG_DICT_UNSERIALIZE_FAIL,
                                "failed to "
                                "unserialize req-buffer to dictionary");
                        snprintf (err_str, sizeof (err_str), "Unable to decode "
                                  "the command");
                        goto out;
                } else {
                        dict->extra_stdfree = cli_req.dict.dict_val;
                }
        }

        gf_msg_trace (this->name, 0, "Received global option request");

        ret = glusterd_op_begin_synctask (req, GD_OP_GANESHA, dict);
out:
        if (ret) {
                if (err_str[0] == '\0')
                        snprintf (err_str, sizeof (err_str),
                                  "Operation failed");
                ret = glusterd_op_send_cli_response (cli_op, ret, 0, req,
                                                     dict, err_str);
        }
        if (op_errstr)
                GF_FREE (op_errstr);
        if (dict)
                dict_unref(dict);

        return ret;
}


int
glusterd_handle_ganesha_cmd (rpcsvc_request_t *req)
{
        return glusterd_big_locked_handler (req, __glusterd_handle_ganesha_cmd);
}

static int
__glusterd_handle_reset_volume (rpcsvc_request_t *req)
{
        int32_t                         ret = -1;
        gf_cli_req                      cli_req = {{0,}};
        dict_t                          *dict = NULL;
        glusterd_op_t                   cli_op = GD_OP_RESET_VOLUME;
        char                            *volname = NULL;
        char                            err_str[2048] = {0,};
        xlator_t                        *this = NULL;

        GF_ASSERT (req);
        this = THIS;
        GF_ASSERT (this);

        gf_msg (this->name, GF_LOG_INFO, 0, 0,
                "Received reset vol req");

        ret = xdr_to_generic (req->msg[0], &cli_req, (xdrproc_t)xdr_gf_cli_req);
        if (ret < 0) {
                snprintf (err_str, sizeof (err_str), "Failed to decode request "
                          "received from cli");
                gf_msg (this->name, GF_LOG_ERROR, 0,
                        GD_MSG_REQ_DECODE_FAIL, "%s", err_str);
                req->rpc_err = GARBAGE_ARGS;
                goto out;
        }

        if (cli_req.dict.dict_len) {
                /* Unserialize the dictionary */
                dict  = dict_new ();

                ret = dict_unserialize (cli_req.dict.dict_val,
                                        cli_req.dict.dict_len,
                                        &dict);
                if (ret < 0) {
                        gf_msg (this->name, GF_LOG_ERROR, 0,
                                GD_MSG_DICT_UNSERIALIZE_FAIL, "failed to "
                                "unserialize req-buffer to dictionary");
                        snprintf (err_str, sizeof (err_str), "Unable to decode "
                                  "the command");
                        goto out;
                } else {
                        dict->extra_stdfree = cli_req.dict.dict_val;
                }
        }

        ret = dict_get_str (dict, "volname", &volname);
        if (ret) {
                snprintf (err_str, sizeof (err_str), "Failed to get volume "
                          "name");
                gf_msg (this->name, GF_LOG_ERROR, 0,
                        GD_MSG_VOLNAME_NOTFOUND_IN_DICT, "%s", err_str);
                goto out;
        }
        gf_msg_debug (this->name, 0, "Received volume reset request for "
                "volume %s", volname);

        ret = glusterd_op_begin_synctask (req, GD_OP_RESET_VOLUME, dict);

out:
        if (ret) {
                if (err_str[0] == '\0')
                        snprintf (err_str, sizeof (err_str),
                                  "Operation failed");
                ret = glusterd_op_send_cli_response (cli_op, ret, 0, req,
                                                     dict, err_str);
        }

        return ret;
}

int
glusterd_handle_reset_volume (rpcsvc_request_t *req)
{
        return glusterd_big_locked_handler (req,
                                            __glusterd_handle_reset_volume);
}

int
__glusterd_handle_set_volume (rpcsvc_request_t *req)
{
        int32_t                         ret = -1;
        gf_cli_req                      cli_req = {{0,}};
        dict_t                          *dict = NULL;
        glusterd_op_t                   cli_op = GD_OP_SET_VOLUME;
        char                            *key = NULL;
        char                            *value = NULL;
        char                            *volname = NULL;
        char                            *op_errstr = NULL;
        gf_boolean_t                    help = _gf_false;
        char                            err_str[2048] = {0,};
        xlator_t                        *this = NULL;

        this = THIS;
        GF_ASSERT (this);

        GF_ASSERT (req);

        ret = xdr_to_generic (req->msg[0], &cli_req, (xdrproc_t)xdr_gf_cli_req);
        if (ret < 0) {
                snprintf (err_str, sizeof (err_str), "Failed to decode "
                          "request received from cli");
                gf_msg (this->name, GF_LOG_ERROR, 0,
                        GD_MSG_REQ_DECODE_FAIL, "%s", err_str);
                req->rpc_err = GARBAGE_ARGS;
                goto out;
        }

        if (cli_req.dict.dict_len) {
                /* Unserialize the dictionary */
                dict  = dict_new ();

                ret = dict_unserialize (cli_req.dict.dict_val,
                                        cli_req.dict.dict_len,
                                        &dict);
                if (ret < 0) {
                        gf_msg (this->name, GF_LOG_ERROR, errno,
                                GD_MSG_DICT_UNSERIALIZE_FAIL,
                                "failed to "
                                "unserialize req-buffer to dictionary");
                        snprintf (err_str, sizeof (err_str), "Unable to decode "
                                  "the command");
                        goto out;
                } else {
                        dict->extra_stdfree = cli_req.dict.dict_val;
                }
        }

        ret = dict_get_str (dict, "volname", &volname);
        if (ret) {
                snprintf (err_str, sizeof (err_str), "Failed to get volume "
                          "name while handling volume set command");
                gf_msg (this->name, GF_LOG_ERROR, 0,
                        GD_MSG_DICT_GET_FAILED, "%s", err_str);
                goto out;
        }

        if (strcmp (volname, "help") == 0 ||
            strcmp (volname, "help-xml") == 0) {
                ret = glusterd_volset_help (dict, &op_errstr);
                help = _gf_true;
                goto out;
        }

        ret = dict_get_str (dict, "key1", &key);
        if (ret) {
                snprintf (err_str, sizeof (err_str), "Failed to get key while"
                          " handling volume set for %s", volname);
                gf_msg (this->name, GF_LOG_ERROR, 0,
                        GD_MSG_DICT_GET_FAILED, "%s", err_str);
                goto out;
        }

        ret = dict_get_str (dict, "value1", &value);
        if (ret) {
                snprintf (err_str, sizeof (err_str), "Failed to get value while"
                          " handling volume set for %s", volname);
                gf_msg (this->name, GF_LOG_ERROR, 0,
                        GD_MSG_DICT_GET_FAILED, "%s", err_str);
                goto out;
        }
        gf_msg_debug (this->name, 0, "Received volume set request for "
                "volume %s", volname);

        ret = glusterd_op_begin_synctask (req, GD_OP_SET_VOLUME, dict);

out:
        if (help)
                ret = glusterd_op_send_cli_response (cli_op, ret, 0, req, dict,
                                                     (op_errstr)? op_errstr:"");
        else if (ret) {
                if (err_str[0] == '\0')
                        snprintf (err_str, sizeof (err_str),
                                  "Operation failed");
                ret = glusterd_op_send_cli_response (cli_op, ret, 0, req,
                                                     dict, err_str);
        }
        if (op_errstr)
                GF_FREE (op_errstr);

        return ret;
}

int
glusterd_handle_set_volume (rpcsvc_request_t *req)
{
        return glusterd_big_locked_handler (req, __glusterd_handle_set_volume);
}

int
__glusterd_handle_sync_volume (rpcsvc_request_t *req)
{
        int32_t                          ret     = -1;
        gf_cli_req                       cli_req = {{0,}};
        dict_t                           *dict = NULL;
        gf_cli_rsp                       cli_rsp = {0.};
        char                             msg[2048] = {0,};
        char                             *volname = NULL;
        gf1_cli_sync_volume              flags = 0;
        char                             *hostname = NULL;
        xlator_t                         *this = NULL;

        GF_ASSERT (req);
        this = THIS;
        GF_ASSERT (this);

        ret = xdr_to_generic (req->msg[0], &cli_req, (xdrproc_t)xdr_gf_cli_req);
        if (ret < 0) {
                //failed to decode msg;
                gf_msg (this->name, GF_LOG_ERROR, 0,
                        GD_MSG_REQ_DECODE_FAIL, "%s", "Failed to decode "
                        "request received from cli");
                req->rpc_err = GARBAGE_ARGS;
                goto out;
        }

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
                } else {
                        dict->extra_stdfree = cli_req.dict.dict_val;
                }
        }

        ret = dict_get_str (dict, "hostname", &hostname);
        if (ret) {
                snprintf (msg, sizeof (msg), "Failed to get hostname");
                gf_msg (this->name, GF_LOG_ERROR, 0,
                        GD_MSG_HOSTNAME_NOTFOUND_IN_DICT, "%s", msg);
                goto out;
        }

        ret = dict_get_str (dict, "volname", &volname);
        if (ret) {
                ret = dict_get_int32 (dict, "flags", (int32_t*)&flags);
                if (ret) {
                        snprintf (msg, sizeof (msg), "Failed to get volume name"
                                  " or flags");
                        gf_msg (this->name, GF_LOG_ERROR, 0,
                                GD_MSG_FLAGS_NOTFOUND_IN_DICT, "%s", msg);
                        goto out;
                }
        }

        gf_msg (this->name, GF_LOG_INFO, 0,
                GD_MSG_VOL_SYNC_REQ_RCVD, "Received volume sync req "
                "for volume %s", (flags & GF_CLI_SYNC_ALL) ? "all" : volname);

        if (gf_is_local_addr (hostname)) {
                ret = -1;
                snprintf (msg, sizeof (msg), "sync from localhost"
                          " not allowed");
                gf_msg (this->name, GF_LOG_ERROR, 0,
                        GD_MSG_SYNC_FROM_LOCALHOST_UNALLOWED, "%s", msg);
                goto out;
        }

        ret = glusterd_op_begin_synctask (req, GD_OP_SYNC_VOLUME, dict);

out:
        if (ret) {
                cli_rsp.op_ret = -1;
                cli_rsp.op_errstr = msg;
                if (msg[0] == '\0')
                        snprintf (msg, sizeof (msg), "Operation failed");
                glusterd_to_cli (req, &cli_rsp, NULL, 0, NULL,
                                 (xdrproc_t)xdr_gf_cli_rsp, dict);

                ret = 0; //sent error to cli, prevent second reply
        }

        return ret;
}

int
glusterd_handle_sync_volume (rpcsvc_request_t *req)
{
        return glusterd_big_locked_handler (req, __glusterd_handle_sync_volume);
}

int
glusterd_fsm_log_send_resp (rpcsvc_request_t *req, int op_ret,
                            char *op_errstr, dict_t *dict)
{

        int                             ret = -1;
        gf1_cli_fsm_log_rsp             rsp = {0};

        GF_ASSERT (req);
        GF_ASSERT (op_errstr);

        rsp.op_ret = op_ret;
        rsp.op_errstr = op_errstr;
        if (rsp.op_ret == 0)
                ret = dict_allocate_and_serialize (dict, &rsp.fsm_log.fsm_log_val,
                                                &rsp.fsm_log.fsm_log_len);

        ret = glusterd_submit_reply (req, &rsp, NULL, 0, NULL,
                                     (xdrproc_t)xdr_gf1_cli_fsm_log_rsp);
        GF_FREE (rsp.fsm_log.fsm_log_val);

        gf_msg_debug ("glusterd", 0, "Responded, ret: %d", ret);

        return 0;
}

int
__glusterd_handle_fsm_log (rpcsvc_request_t *req)
{
        int32_t                         ret = -1;
        gf1_cli_fsm_log_req             cli_req = {0,};
        dict_t                          *dict = NULL;
        xlator_t                        *this = NULL;
        glusterd_conf_t                 *conf = NULL;
        char                            msg[2048] = {0};
        glusterd_peerinfo_t             *peerinfo = NULL;

        GF_ASSERT (req);

        this = THIS;
        GF_VALIDATE_OR_GOTO ("xlator", (this != NULL), out);

        ret = xdr_to_generic (req->msg[0], &cli_req,
                              (xdrproc_t)xdr_gf1_cli_fsm_log_req);
        if (ret < 0) {
                //failed to decode msg;
                gf_msg (this->name, GF_LOG_ERROR, 0,
                        GD_MSG_REQ_DECODE_FAIL, "Failed to decode "
                        "request received from client.");
                req->rpc_err = GARBAGE_ARGS;
                snprintf (msg, sizeof (msg), "Garbage request");
                goto out;
        }

        dict = dict_new ();
        if (!dict) {
                ret = -1;
                goto out;
        }

        if (strcmp ("", cli_req.name) == 0) {
                conf = this->private;
                ret = glusterd_sm_tr_log_add_to_dict (dict, &conf->op_sm_log);
        } else {
                rcu_read_lock ();

                peerinfo = glusterd_peerinfo_find_by_hostname (cli_req.name);
                if (!peerinfo) {
                        ret = -1;
                        snprintf (msg, sizeof (msg), "%s is not a peer",
                                  cli_req.name);
                } else {
                        ret = glusterd_sm_tr_log_add_to_dict
                                (dict, &peerinfo->sm_log);
                }

                rcu_read_unlock ();
        }

out:
        (void)glusterd_fsm_log_send_resp (req, ret, msg, dict);
        free (cli_req.name);//malloced by xdr
        if (dict)
                dict_unref (dict);

        glusterd_friend_sm ();
        glusterd_op_sm ();

        return 0;//send 0 to avoid double reply
}

int
glusterd_handle_fsm_log (rpcsvc_request_t *req)
{
        return glusterd_big_locked_handler (req, __glusterd_handle_fsm_log);
}

int
glusterd_op_lock_send_resp (rpcsvc_request_t *req, int32_t status)
{

        gd1_mgmt_cluster_lock_rsp       rsp = {{0},};
        int                             ret = -1;

        GF_ASSERT (req);
        glusterd_get_uuid (&rsp.uuid);
        rsp.op_ret = status;

        ret = glusterd_submit_reply (req, &rsp, NULL, 0, NULL,
                                     (xdrproc_t)xdr_gd1_mgmt_cluster_lock_rsp);

        gf_msg_debug (THIS->name, 0, "Responded to lock, ret: %d", ret);

        return 0;
}

int
glusterd_op_unlock_send_resp (rpcsvc_request_t *req, int32_t status)
{

        gd1_mgmt_cluster_unlock_rsp     rsp = {{0},};
        int                             ret = -1;

        GF_ASSERT (req);
        rsp.op_ret = status;
        glusterd_get_uuid (&rsp.uuid);

        ret = glusterd_submit_reply (req, &rsp, NULL, 0, NULL,
                                     (xdrproc_t)xdr_gd1_mgmt_cluster_unlock_rsp);

        gf_msg_debug (THIS->name, 0, "Responded to unlock, ret: %d", ret);

        return ret;
}

int
glusterd_op_mgmt_v3_lock_send_resp (rpcsvc_request_t *req, uuid_t *txn_id,
                                   int32_t status)
{

        gd1_mgmt_v3_lock_rsp    rsp = {{0},};
        int                     ret = -1;

        GF_ASSERT (req);
        GF_ASSERT (txn_id);
        glusterd_get_uuid (&rsp.uuid);
        rsp.op_ret = status;
        if (rsp.op_ret)
               rsp.op_errno = errno;
        gf_uuid_copy (rsp.txn_id, *txn_id);

        ret = glusterd_submit_reply (req, &rsp, NULL, 0, NULL,
                                     (xdrproc_t)xdr_gd1_mgmt_v3_lock_rsp);

        gf_msg_debug (THIS->name, 0, "Responded to mgmt_v3 lock, ret: %d",
                ret);

        return ret;
}

int
glusterd_op_mgmt_v3_unlock_send_resp (rpcsvc_request_t *req, uuid_t *txn_id,
                                     int32_t status)
{

        gd1_mgmt_v3_unlock_rsp      rsp = {{0},};
        int                             ret = -1;

        GF_ASSERT (req);
        GF_ASSERT (txn_id);
        rsp.op_ret = status;
        if (rsp.op_ret)
               rsp.op_errno = errno;
        glusterd_get_uuid (&rsp.uuid);
        gf_uuid_copy (rsp.txn_id, *txn_id);

        ret = glusterd_submit_reply (req, &rsp, NULL, 0, NULL,
                                     (xdrproc_t)xdr_gd1_mgmt_v3_unlock_rsp);

        gf_msg_debug (THIS->name, 0, "Responded to mgmt_v3 unlock, ret: %d",
                ret);

        return ret;
}

int
__glusterd_handle_cluster_unlock (rpcsvc_request_t *req)
{
        gd1_mgmt_cluster_unlock_req     unlock_req = {{0}, };
        int32_t                         ret = -1;
        glusterd_op_lock_ctx_t          *ctx = NULL;
        xlator_t                        *this = NULL;
        uuid_t                          *txn_id = NULL;
        glusterd_conf_t                 *priv = NULL;

        this = THIS;
        GF_ASSERT (this);
        priv = this->private;
        GF_ASSERT (priv);
        GF_ASSERT (req);

        txn_id = &priv->global_txn_id;

        ret = xdr_to_generic (req->msg[0], &unlock_req,
                              (xdrproc_t)xdr_gd1_mgmt_cluster_unlock_req);
        if (ret < 0) {
                gf_msg (this->name, GF_LOG_ERROR, 0,
                        GD_MSG_REQ_DECODE_FAIL, "Failed to decode unlock "
                        "request received from peer");
                req->rpc_err = GARBAGE_ARGS;
                goto out;
        }


        gf_msg_debug (this->name, 0,
                "Received UNLOCK from uuid: %s", uuid_utoa (unlock_req.uuid));

        rcu_read_lock ();
        ret = (glusterd_peerinfo_find_by_uuid (unlock_req.uuid) == NULL);
        rcu_read_unlock ();
        if (ret) {
                gf_msg (this->name, GF_LOG_WARNING, 0,
                        GD_MSG_PEER_NOT_FOUND, "%s doesn't "
                        "belong to the cluster. Ignoring request.",
                        uuid_utoa (unlock_req.uuid));
                ret = -1;
                goto out;
        }

        ctx = GF_CALLOC (1, sizeof (*ctx), gf_gld_mt_op_lock_ctx_t);

        if (!ctx) {
                //respond here
                gf_msg (this->name, GF_LOG_ERROR, ENOMEM,
                        GD_MSG_NO_MEMORY, "No memory.");
                return -1;
        }
        gf_uuid_copy (ctx->uuid, unlock_req.uuid);
        ctx->req = req;
        ctx->dict = NULL;

        ret = glusterd_op_sm_inject_event (GD_OP_EVENT_UNLOCK, txn_id, ctx);

out:
        glusterd_friend_sm ();
        glusterd_op_sm ();

        return ret;
}

int
glusterd_handle_cluster_unlock (rpcsvc_request_t *req)
{
        return glusterd_big_locked_handler (req,
                                            __glusterd_handle_cluster_unlock);
}

int
glusterd_op_stage_send_resp (rpcsvc_request_t   *req,
                             int32_t op, int32_t status,
                             char *op_errstr, dict_t *rsp_dict)
{
        gd1_mgmt_stage_op_rsp           rsp      = {{0},};
        int                             ret      = -1;
        xlator_t                       *this     = NULL;

        this = THIS;
        GF_ASSERT (this);
        GF_ASSERT (req);

        rsp.op_ret = status;
        glusterd_get_uuid (&rsp.uuid);
        rsp.op = op;
        if (op_errstr)
                rsp.op_errstr = op_errstr;
        else
                rsp.op_errstr = "";

        ret = dict_allocate_and_serialize (rsp_dict, &rsp.dict.dict_val,
                                           &rsp.dict.dict_len);
        if (ret < 0) {
                gf_msg (this->name, GF_LOG_ERROR, 0,
                        GD_MSG_DICT_SERL_LENGTH_GET_FAIL,
                        "failed to get serialized length of dict");
                return ret;
        }

        ret = glusterd_submit_reply (req, &rsp, NULL, 0, NULL,
                                     (xdrproc_t)xdr_gd1_mgmt_stage_op_rsp);

        gf_msg_debug (this->name, 0, "Responded to stage, ret: %d", ret);
        GF_FREE (rsp.dict.dict_val);

        return ret;
}

int
glusterd_op_commit_send_resp (rpcsvc_request_t *req,
                               int32_t op, int32_t status, char *op_errstr,
                               dict_t *rsp_dict)
{
        gd1_mgmt_commit_op_rsp          rsp      = {{0}, };
        int                             ret      = -1;
        xlator_t                        *this = NULL;

        this = THIS;
        GF_ASSERT (this);
        GF_ASSERT (req);
        rsp.op_ret = status;
        glusterd_get_uuid (&rsp.uuid);
        rsp.op = op;

        if (op_errstr)
                rsp.op_errstr = op_errstr;
        else
                rsp.op_errstr = "";

        if (rsp_dict) {
                ret = dict_allocate_and_serialize (rsp_dict, &rsp.dict.dict_val,
                                                   &rsp.dict.dict_len);
                if (ret < 0) {
                        gf_msg (this->name, GF_LOG_ERROR, 0,
                                GD_MSG_DICT_SERL_LENGTH_GET_FAIL,
                                "failed to get serialized length of dict");
                        goto out;
                }
        }


        ret = glusterd_submit_reply (req, &rsp, NULL, 0, NULL,
                                     (xdrproc_t)xdr_gd1_mgmt_commit_op_rsp);

        gf_msg_debug (this->name, 0, "Responded to commit, ret: %d", ret);

out:
        GF_FREE (rsp.dict.dict_val);
        return ret;
}

int
__glusterd_handle_incoming_friend_req (rpcsvc_request_t *req)
{
        int32_t                 ret = -1;
        gd1_mgmt_friend_req     friend_req = {{0},};
        gf_boolean_t            run_fsm = _gf_true;

        GF_ASSERT (req);
        ret = xdr_to_generic (req->msg[0], &friend_req,
                              (xdrproc_t)xdr_gd1_mgmt_friend_req);
        if (ret < 0) {
                //failed to decode msg;
                gf_msg ("glusterd", GF_LOG_ERROR, 0,
                        GD_MSG_REQ_DECODE_FAIL, "Failed to decode "
                        "request received from friend");
                req->rpc_err = GARBAGE_ARGS;
                goto out;
        }

        gf_msg ("glusterd", GF_LOG_INFO, 0,
                GD_MSG_PROBE_RCVD,
                "Received probe from uuid: %s", uuid_utoa (friend_req.uuid));
        ret = glusterd_handle_friend_req (req, friend_req.uuid,
                                          friend_req.hostname, friend_req.port,
                                          &friend_req);

        if (ret == GLUSTERD_CONNECTION_AWAITED) {
                //fsm should be run after connection establishes
                run_fsm = _gf_false;
                ret = 0;
        }

out:
        free (friend_req.hostname);//malloced by xdr

        if (run_fsm) {
                glusterd_friend_sm ();
                glusterd_op_sm ();
        }

        return ret;
}

int
glusterd_handle_incoming_friend_req (rpcsvc_request_t *req)
{
        return glusterd_big_locked_handler (req,
                                            __glusterd_handle_incoming_friend_req);
}

int
__glusterd_handle_incoming_unfriend_req (rpcsvc_request_t *req)
{
        int32_t                 ret = -1;
        gd1_mgmt_friend_req     friend_req = {{0},};
        char               remote_hostname[UNIX_PATH_MAX + 1] = {0,};

        GF_ASSERT (req);
        ret = xdr_to_generic (req->msg[0], &friend_req,
                              (xdrproc_t)xdr_gd1_mgmt_friend_req);
        if (ret < 0) {
                //failed to decode msg;
                gf_msg ("glusterd", GF_LOG_ERROR, 0,
                        GD_MSG_REQ_DECODE_FAIL, "Failed to decode "
                        "request received.");
                req->rpc_err = GARBAGE_ARGS;
                goto out;
        }

        gf_msg ("glusterd", GF_LOG_INFO, 0,
                GD_MSG_UNFRIEND_REQ_RCVD,
                "Received unfriend from uuid: %s", uuid_utoa (friend_req.uuid));

        ret = glusterd_remote_hostname_get (req, remote_hostname,
                                            sizeof (remote_hostname));
        if (ret) {
                gf_msg ("glusterd", GF_LOG_ERROR, 0,
                        GD_MSG_HOSTNAME_RESOLVE_FAIL,
                        "Unable to get the remote hostname");
                goto out;
        }
        ret = glusterd_handle_unfriend_req (req, friend_req.uuid,
                                            remote_hostname, friend_req.port);

out:
        free (friend_req.hostname);//malloced by xdr
        free (friend_req.vols.vols_val);//malloced by xdr

        glusterd_friend_sm ();
        glusterd_op_sm ();

        return ret;
}

int
glusterd_handle_incoming_unfriend_req (rpcsvc_request_t *req)
{
        return glusterd_big_locked_handler (req,
                                            __glusterd_handle_incoming_unfriend_req);

}

int
glusterd_handle_friend_update_delete (dict_t *dict)
{
        char                    *hostname = NULL;
        int32_t                 ret = -1;

        GF_ASSERT (dict);

        ret = dict_get_str (dict, "hostname", &hostname);
        if (ret)
                goto out;

        ret = glusterd_friend_remove (NULL, hostname);

out:
        gf_msg_debug ("glusterd", 0, "Returning %d", ret);
        return ret;
}

int
glusterd_peer_hostname_update (glusterd_peerinfo_t *peerinfo,
                               const char *hostname, gf_boolean_t store_update)
{
        int                     ret = 0;

        GF_ASSERT (peerinfo);
        GF_ASSERT (hostname);

        ret = gd_add_address_to_peer (peerinfo, hostname);
        if (ret) {
                gf_msg (THIS->name, GF_LOG_ERROR, 0,
                        GD_MSG_HOSTNAME_ADD_TO_PEERLIST_FAIL,
                        "Couldn't add address to the peer info");
                goto out;
        }

        if (store_update)
                ret = glusterd_store_peerinfo (peerinfo);
out:
        gf_msg_debug (THIS->name, 0, "Returning %d", ret);
        return ret;
}

int
__glusterd_handle_friend_update (rpcsvc_request_t *req)
{
        int32_t                 ret = -1;
        gd1_mgmt_friend_update     friend_req = {{0},};
        glusterd_peerinfo_t     *peerinfo = NULL;
        glusterd_conf_t         *priv = NULL;
        xlator_t                *this = NULL;
        gd1_mgmt_friend_update_rsp rsp = {{0},};
        dict_t                  *dict = NULL;
        char                    key[100] = {0,};
        char                    *uuid_buf = NULL;
        int                     i = 1;
        int                     count = 0;
        uuid_t                  uuid = {0,};
        glusterd_peerctx_args_t args = {0};
        int32_t                 op = 0;

        GF_ASSERT (req);

        this = THIS;
        GF_ASSERT (this);
        priv = this->private;
        GF_ASSERT (priv);

        ret = xdr_to_generic (req->msg[0], &friend_req,
                              (xdrproc_t)xdr_gd1_mgmt_friend_update);
        if (ret < 0) {
                //failed to decode msg;
                gf_msg (this->name, GF_LOG_ERROR, 0,
                        GD_MSG_REQ_DECODE_FAIL, "Failed to decode "
                        "request received");
                req->rpc_err = GARBAGE_ARGS;
                goto out;
        }

        ret = 0;
        rcu_read_lock ();
        if (glusterd_peerinfo_find (friend_req.uuid, NULL) == NULL) {
                ret = -1;
        }
        rcu_read_unlock ();
        if (ret) {
                gf_msg (this->name, GF_LOG_CRITICAL, 0,
                        GD_MSG_REQ_FROM_UNKNOWN_PEER,
                        "Received friend update request "
                        "from unknown peer %s", uuid_utoa (friend_req.uuid));
                gf_event (EVENT_UNKNOWN_PEER, "peer=%s",
                          uuid_utoa (friend_req.uuid));
                goto out;
        }

        gf_msg ("glusterd", GF_LOG_INFO, 0,
                GD_MSG_FRIEND_UPDATE_RCVD,
                "Received friend update from uuid: %s", uuid_utoa (friend_req.uuid));

        if (friend_req.friends.friends_len) {
                /* Unserialize the dictionary */
                dict  = dict_new ();

                ret = dict_unserialize (friend_req.friends.friends_val,
                                        friend_req.friends.friends_len,
                                        &dict);
                if (ret < 0) {
                        gf_msg ("glusterd", GF_LOG_ERROR, 0,
                                GD_MSG_DICT_UNSERIALIZE_FAIL,
                                "failed to "
                                "unserialize req-buffer to dictionary");
                        goto out;
                } else {
                        dict->extra_stdfree = friend_req.friends.friends_val;
                }
        }

        ret = dict_get_int32 (dict, "count", &count);
        if (ret)
                goto out;

        ret = dict_get_int32 (dict, "op", &op);
        if (ret)
                goto out;

        if (GD_FRIEND_UPDATE_DEL == op) {
                ret = glusterd_handle_friend_update_delete (dict);
                goto out;
        }

        args.mode = GD_MODE_ON;
        while ( i <= count) {
                memset (key, 0, sizeof (key));
                snprintf (key, sizeof (key), "friend%d.uuid", i);
                ret = dict_get_str (dict, key, &uuid_buf);
                if (ret)
                        goto out;
                gf_uuid_parse (uuid_buf, uuid);

                if (!gf_uuid_compare (uuid, MY_UUID)) {
                        gf_msg (this->name, GF_LOG_INFO, 0,
                                GD_MSG_UUID_RECEIVED,
                                "Received my uuid as Friend");
                        i++;
                        continue;
                }

                memset (key, 0, sizeof (key));
                snprintf (key, sizeof (key), "friend%d", i);

                rcu_read_lock ();
                peerinfo = glusterd_peerinfo_find (uuid, NULL);
                if (peerinfo == NULL) {
                        /* Create a new peer and add it to the list as there is
                         * no existing peer with the uuid
                         */
                        peerinfo = gd_peerinfo_from_dict (dict, key);
                        if (peerinfo == NULL) {
                                ret = -1;
                                gf_msg (this->name, GF_LOG_ERROR, 0,
                                        GD_MSG_PEERINFO_CREATE_FAIL,
                                        "Could not create peerinfo from dict "
                                        "for prefix %s", key);
                                goto unlock;
                        }

                        /* As this is a new peer, it should be added as a
                         * friend.  The friend state machine will take care of
                         * correcting the state as required
                         */
                        peerinfo->state.state = GD_FRIEND_STATE_BEFRIENDED;

                        ret = glusterd_friend_add_from_peerinfo (peerinfo, 0,
                                                                 &args);
                } else {
                        /* As an existing peer was found, update it with the new
                         * information
                         */
                        ret = gd_update_peerinfo_from_dict (peerinfo, dict,
                                                            key);
                        if (ret) {
                                gf_msg (this->name, GF_LOG_ERROR, 0,
                                        GD_MSG_PEER_INFO_UPDATE_FAIL,
                                        "Failed to "
                                        "update peer %s", peerinfo->hostname);
                                goto unlock;
                        }
                        ret = glusterd_store_peerinfo (peerinfo);
                        if (ret) {
                                gf_msg (this->name, GF_LOG_ERROR, 0,
                                        GD_MSG_PEERINFO_CREATE_FAIL,
                                        "Failed to store peerinfo");
                                gf_event (EVENT_PEER_STORE_FAILURE, "peer=%s",
                                          peerinfo->hostname);
                        }
                }
unlock:
                rcu_read_unlock ();
                if (ret)
                        break;

                peerinfo = NULL;
                i++;
        }

out:
        gf_uuid_copy (rsp.uuid, MY_UUID);
        ret = glusterd_submit_reply (req, &rsp, NULL, 0, NULL,
                                     (xdrproc_t)xdr_gd1_mgmt_friend_update_rsp);
        if (dict) {
                if (!dict->extra_stdfree && friend_req.friends.friends_val)
                        free (friend_req.friends.friends_val);//malloced by xdr
                dict_unref (dict);
        } else {
                free (friend_req.friends.friends_val);//malloced by xdr
        }

        if (peerinfo)
                glusterd_peerinfo_cleanup (peerinfo);

        glusterd_friend_sm ();
        glusterd_op_sm ();

        return ret;
}

int
glusterd_handle_friend_update (rpcsvc_request_t *req)
{
        return glusterd_big_locked_handler (req,
                                            __glusterd_handle_friend_update);
}

int
__glusterd_handle_probe_query (rpcsvc_request_t *req)
{
        int32_t                         ret = -1;
        xlator_t                        *this = NULL;
        glusterd_conf_t                 *conf = NULL;
        gd1_mgmt_probe_req              probe_req = {{0},};
        gd1_mgmt_probe_rsp              rsp = {{0},};
        glusterd_peerinfo_t             *peerinfo = NULL;
        glusterd_peerctx_args_t         args = {0};
        int                             port = 0;
        char               remote_hostname[UNIX_PATH_MAX + 1] = {0,};

        GF_ASSERT (req);

        this = THIS;
        GF_VALIDATE_OR_GOTO ("xlator", (this != NULL), out);

        ret = xdr_to_generic (req->msg[0], &probe_req,
                              (xdrproc_t)xdr_gd1_mgmt_probe_req);
        if (ret < 0) {
                //failed to decode msg;
                gf_msg (this->name, GF_LOG_ERROR, 0,
                        GD_MSG_REQ_DECODE_FAIL, "Failed to decode probe "
                        "request");
                req->rpc_err = GARBAGE_ARGS;
                goto out;
        }

        conf = this->private;
        if (probe_req.port)
                port = probe_req.port;
        else
                port = GF_DEFAULT_BASE_PORT;

        gf_msg ("glusterd", GF_LOG_INFO, 0,
                GD_MSG_PROBE_RCVD,
                "Received probe from uuid: %s", uuid_utoa (probe_req.uuid));

        /* Check for uuid collision and handle it in a user friendly way by
         * sending the error.
         */
        if (!gf_uuid_compare (probe_req.uuid, MY_UUID)) {
                gf_msg (THIS->name, GF_LOG_ERROR, 0,
                        GD_MSG_UUIDS_SAME_RETRY, "Peer uuid %s is same as "
                        "local uuid. Please check the uuid of both the peers "
                        "from %s/%s", uuid_utoa (probe_req.uuid),
                        GLUSTERD_DEFAULT_WORKDIR, GLUSTERD_INFO_FILE);
                rsp.op_ret = -1;
                rsp.op_errno = GF_PROBE_SAME_UUID;
                rsp.port = port;
                goto respond;
        }

        ret = glusterd_remote_hostname_get (req, remote_hostname,
                                            sizeof (remote_hostname));
        if (ret) {
                gf_msg ("glusterd", GF_LOG_ERROR, 0,
                        GD_MSG_HOSTNAME_RESOLVE_FAIL,
                        "Unable to get the remote hostname");
                goto out;
        }

        rcu_read_lock ();
        peerinfo = glusterd_peerinfo_find (probe_req.uuid, remote_hostname);
        if ((peerinfo == NULL) && (!cds_list_empty (&conf->peers))) {
                rsp.op_ret = -1;
                rsp.op_errno = GF_PROBE_ANOTHER_CLUSTER;
        } else if (peerinfo == NULL) {
                gf_msg ("glusterd", GF_LOG_INFO, 0,
                        GD_MSG_PEER_NOT_FOUND,
                        "Unable to find peerinfo"
                        " for host: %s (%d)", remote_hostname, port);
                args.mode = GD_MODE_ON;
                ret = glusterd_friend_add (remote_hostname, port,
                                           GD_FRIEND_STATE_PROBE_RCVD,
                                           NULL, &peerinfo, 0, &args);
                if (ret) {
                        gf_msg ("glusterd", GF_LOG_ERROR, 0,
                                GD_MSG_PEER_ADD_FAIL,
                                "Failed to add peer %s",
                                remote_hostname);
                        rsp.op_errno = GF_PROBE_ADD_FAILED;
                }
        }
        rcu_read_unlock ();

respond:
        gf_uuid_copy (rsp.uuid, MY_UUID);

        rsp.hostname = probe_req.hostname;
        rsp.op_errstr = "";

        glusterd_submit_reply (req, &rsp, NULL, 0, NULL,
                               (xdrproc_t)xdr_gd1_mgmt_probe_rsp);
        ret = 0;

        gf_msg ("glusterd", GF_LOG_INFO, 0,
                GD_MSG_RESPONSE_INFO, "Responded to %s, op_ret: %d, "
                "op_errno: %d, ret: %d", remote_hostname,
                rsp.op_ret, rsp.op_errno, ret);

out:
        free (probe_req.hostname);//malloced by xdr

        glusterd_friend_sm ();
        glusterd_op_sm ();

        return ret;
}

int glusterd_handle_probe_query (rpcsvc_request_t *req)
{
        return glusterd_big_locked_handler (req, __glusterd_handle_probe_query);
}

int
__glusterd_handle_cli_profile_volume (rpcsvc_request_t *req)
{
        int32_t                         ret     = -1;
        gf_cli_req                      cli_req = {{0,}};
        dict_t                          *dict = NULL;
        glusterd_op_t                   cli_op = GD_OP_PROFILE_VOLUME;
        char                            *volname = NULL;
        int32_t                         op = 0;
        char                            err_str[2048] = {0,};
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

        if (cli_req.dict.dict_len > 0) {
                dict = dict_new();
                if (!dict)
                        goto out;
                dict_unserialize (cli_req.dict.dict_val,
                                  cli_req.dict.dict_len, &dict);
        }

        ret = dict_get_str (dict, "volname", &volname);
        if (ret) {
                snprintf (err_str, sizeof (err_str), "Unable to get volume "
                          "name");
                gf_msg (this->name, GF_LOG_ERROR, 0,
                        GD_MSG_VOLNAME_NOTFOUND_IN_DICT, "%s", err_str);
                goto out;
        }

        gf_msg (this->name, GF_LOG_INFO, 0,
                GD_MSG_VOL_PROFILE_REQ_RCVD,
                "Received volume profile req "
                "for volume %s", volname);
        ret = dict_get_int32 (dict, "op", &op);
        if (ret) {
                snprintf (err_str, sizeof (err_str), "Unable to get operation");
                gf_msg (this->name, GF_LOG_ERROR, 0,
                        GD_MSG_DICT_GET_FAILED, "%s", err_str);
                goto out;
        }

        ret = glusterd_op_begin (req, cli_op, dict, err_str, sizeof (err_str));

out:
        glusterd_friend_sm ();
        glusterd_op_sm ();

        free (cli_req.dict.dict_val);

        if (ret) {
                if (err_str[0] == '\0')
                        snprintf (err_str, sizeof (err_str),
                                  "Operation failed");
                ret = glusterd_op_send_cli_response (cli_op, ret, 0, req,
                                                     dict, err_str);
        }

        gf_msg_debug (this->name, 0, "Returning %d", ret);
        return ret;
}

int
glusterd_handle_cli_profile_volume (rpcsvc_request_t *req)
{
        return glusterd_big_locked_handler (req,
                                            __glusterd_handle_cli_profile_volume);
}

int
__glusterd_handle_getwd (rpcsvc_request_t *req)
{
        int32_t                 ret = -1;
        gf1_cli_getwd_rsp     rsp = {0,};
        glusterd_conf_t         *priv = NULL;

        GF_ASSERT (req);

        priv = THIS->private;
        GF_ASSERT (priv);

        gf_msg ("glusterd", GF_LOG_INFO, 0,
                GD_MSG_GETWD_REQ_RCVD, "Received getwd req");

        rsp.wd = priv->workdir;

        glusterd_submit_reply (req, &rsp, NULL, 0, NULL,
                               (xdrproc_t)xdr_gf1_cli_getwd_rsp);
        ret = 0;

        glusterd_friend_sm ();
        glusterd_op_sm ();

        return ret;
}

int
glusterd_handle_getwd (rpcsvc_request_t *req)
{
        return glusterd_big_locked_handler (req, __glusterd_handle_getwd);
}

int
__glusterd_handle_mount (rpcsvc_request_t *req)
{
        gf1_cli_mount_req mnt_req = {0,};
        gf1_cli_mount_rsp rsp     = {0,};
        dict_t *dict              = NULL;
        int ret                   = 0;
        glusterd_conf_t     *priv   = NULL;

        GF_ASSERT (req);
	priv = THIS->private;

        ret = xdr_to_generic (req->msg[0], &mnt_req,
                              (xdrproc_t)xdr_gf1_cli_mount_req);
        if (ret < 0) {
                //failed to decode msg;
                gf_msg ("glusterd", GF_LOG_ERROR, 0,
                        GD_MSG_REQ_DECODE_FAIL, "Failed to decode mount "
                        "request received");
                req->rpc_err = GARBAGE_ARGS;
                rsp.op_ret = -1;
                rsp.op_errno = EINVAL;
                goto out;
        }

        gf_msg ("glusterd", GF_LOG_INFO, 0,
                GD_MSG_MOUNT_REQ_RCVD,
                "Received mount req");

        if (mnt_req.dict.dict_len) {
                /* Unserialize the dictionary */
                dict  = dict_new ();

                ret = dict_unserialize (mnt_req.dict.dict_val,
                                        mnt_req.dict.dict_len,
                                        &dict);
                if (ret < 0) {
                        gf_msg ("glusterd", GF_LOG_ERROR, 0,
                                GD_MSG_DICT_UNSERIALIZE_FAIL,
                                "failed to "
                                "unserialize req-buffer to dictionary");
                        rsp.op_ret = -1;
                        rsp.op_errno = -EINVAL;
                        goto out;
                } else {
                        dict->extra_stdfree = mnt_req.dict.dict_val;
                }
        }

	synclock_unlock (&priv->big_lock);
        rsp.op_ret = glusterd_do_mount (mnt_req.label, dict,
                                        &rsp.path, &rsp.op_errno);
	synclock_lock (&priv->big_lock);

 out:
        if (!rsp.path)
                rsp.path = "";

        glusterd_submit_reply (req, &rsp, NULL, 0, NULL,
                               (xdrproc_t)xdr_gf1_cli_mount_rsp);
        ret = 0;

        if (dict)
                dict_unref (dict);
        if (*rsp.path)
                GF_FREE (rsp.path);

        glusterd_friend_sm ();
        glusterd_op_sm ();

        return ret;
}

int
glusterd_handle_mount (rpcsvc_request_t *req)
{
        return glusterd_big_locked_handler (req, __glusterd_handle_mount);
}

int
__glusterd_handle_umount (rpcsvc_request_t *req)
{
        gf1_cli_umount_req umnt_req = {0,};
        gf1_cli_umount_rsp rsp      = {0,};
        char *mountbroker_root      = NULL;
        char mntp[PATH_MAX]         = {0,};
        char *path                  = NULL;
        runner_t runner             = {0,};
        int ret                     = 0;
        xlator_t *this              = THIS;
        gf_boolean_t dir_ok         = _gf_false;
        char *pdir                  = NULL;
        char *t                     = NULL;
        glusterd_conf_t     *priv   = NULL;

        GF_ASSERT (req);
        GF_ASSERT (this);
        priv = this->private;

        ret = xdr_to_generic (req->msg[0], &umnt_req,
                              (xdrproc_t)xdr_gf1_cli_umount_req);
        if (ret < 0) {
                //failed to decode msg;
                gf_msg (this->name, GF_LOG_ERROR, 0,
                        GD_MSG_REQ_DECODE_FAIL, "Failed to decode umount"
                        "request");
                req->rpc_err = GARBAGE_ARGS;
                rsp.op_ret = -1;
                goto out;
        }

        gf_msg ("glusterd", GF_LOG_INFO, 0,
                GD_MSG_UMOUNT_REQ_RCVD,
                "Received umount req");

        if (dict_get_str (this->options, "mountbroker-root",
                          &mountbroker_root) != 0) {
                rsp.op_errno = ENOENT;
                goto out;
        }

        /* check if it is allowed to umount path */
        path = gf_strdup (umnt_req.path);
        if (!path) {
                rsp.op_errno = ENOMEM;
                goto out;
        }
        dir_ok = _gf_false;
        pdir = dirname (path);
        t = strtail (pdir, mountbroker_root);
        if (t && *t == '/') {
                t = strtail(++t, MB_HIVE);
                if (t && !*t)
                        dir_ok = _gf_true;
        }
        GF_FREE (path);
        if (!dir_ok) {
                rsp.op_errno = EACCES;
                goto out;
        }

        synclock_unlock (&priv->big_lock);

        if (umnt_req.lazy) {
                rsp.op_ret = gf_umount_lazy (this->name, umnt_req.path, 0);
        } else {
                runinit (&runner);
                runner_add_args (&runner, _PATH_UMOUNT, umnt_req.path, NULL);
                rsp.op_ret = runner_run (&runner);
        }

        synclock_lock (&priv->big_lock);
        if (rsp.op_ret == 0) {
                if (realpath (umnt_req.path, mntp))
                        sys_rmdir (mntp);
                else {
                        rsp.op_ret = -1;
                        rsp.op_errno = errno;
                }
                if (sys_unlink (umnt_req.path) != 0) {
                        rsp.op_ret = -1;
                        rsp.op_errno = errno;
                }
        }

 out:
        if (rsp.op_errno)
                rsp.op_ret = -1;

        glusterd_submit_reply (req, &rsp, NULL, 0, NULL,
                               (xdrproc_t)xdr_gf1_cli_umount_rsp);
        ret = 0;

        glusterd_friend_sm ();
        glusterd_op_sm ();

        return ret;
}

int
glusterd_handle_umount (rpcsvc_request_t *req)
{
        return glusterd_big_locked_handler (req, __glusterd_handle_umount);
}

int
glusterd_friend_remove (uuid_t uuid, char *hostname)
{
        int                           ret = -1;
        glusterd_peerinfo_t           *peerinfo = NULL;

        rcu_read_lock ();

        peerinfo = glusterd_peerinfo_find (uuid, hostname);
        if (peerinfo == NULL) {
                rcu_read_unlock ();
                goto out;
        }

        ret = glusterd_friend_remove_cleanup_vols (peerinfo->uuid);
        if (ret)
                gf_msg (THIS->name, GF_LOG_WARNING, 0,
                        GD_MSG_VOL_CLEANUP_FAIL, "Volumes cleanup failed");
        rcu_read_unlock ();
        /* Giving up the critical section here as glusterd_peerinfo_cleanup must
         * be called from outside a critical section
         */
        ret = glusterd_peerinfo_cleanup (peerinfo);
out:
        gf_msg_debug (THIS->name, 0, "returning %d", ret);
        return ret;
}

int
glusterd_rpc_create (struct rpc_clnt **rpc,
                     dict_t *options,
                     rpc_clnt_notify_t notify_fn,
                     void *notify_data,
                     gf_boolean_t force)
{
        struct rpc_clnt         *new_rpc = NULL;
        int                     ret = -1;
        xlator_t                *this = NULL;

        this = THIS;
        GF_ASSERT (this);

        GF_ASSERT (options);

        if (force && rpc && *rpc) {
                (void) rpc_clnt_unref (*rpc);
                *rpc = NULL;
        }

        /* TODO: is 32 enough? or more ? */
        new_rpc = rpc_clnt_new (options, this, this->name, 16);
        if (!new_rpc)
                goto out;

        ret = rpc_clnt_register_notify (new_rpc, notify_fn, notify_data);
        *rpc = new_rpc;
        if (ret)
                goto out;
        ret = rpc_clnt_start (new_rpc);
out:
        if (ret) {
                if (new_rpc) {
                        (void) rpc_clnt_unref (new_rpc);
                }
        }

        gf_msg_debug (this->name, 0, "returning %d", ret);
        return ret;
}

int
glusterd_transport_inet_options_build (dict_t **options, const char *hostname,
                                       int port)
{
        xlator_t *this = NULL;
        dict_t  *dict = NULL;
        int32_t interval = -1;
        int32_t time     = -1;
        int32_t timeout  = -1;
        int     ret = 0;

        this = THIS;
        GF_ASSERT (this);
        GF_ASSERT (options);
        GF_ASSERT (hostname);

        if (!port)
                port = GLUSTERD_DEFAULT_PORT;

        /* Build default transport options */
        ret = rpc_transport_inet_options_build (&dict, hostname, port);
        if (ret)
                goto out;

        /* Set frame-timeout to 10mins. Default timeout of 30 mins is too long
         * when compared to 2 mins for cli timeout. This ensures users don't
         * wait too long after cli timesout before being able to resume normal
         * operations
         */
        ret = dict_set_int32 (dict, "frame-timeout", 600);
        if (ret) {
                gf_msg ("glusterd", GF_LOG_ERROR, 0,
                        GD_MSG_DICT_SET_FAILED,
                        "Failed to set frame-timeout");
                goto out;
        }

        /* Set keepalive options */
        ret = dict_get_int32 (this->options,
                              "transport.socket.keepalive-interval",
                              &interval);
        if (ret) {
                gf_msg ("glusterd", GF_LOG_WARNING, 0,
                        GD_MSG_DICT_GET_FAILED,
                        "Failed to get socket keepalive-interval");
        }
        ret = dict_get_int32 (this->options,
                              "transport.socket.keepalive-time",
                              &time);
        if (ret) {
                gf_msg ("glusterd", GF_LOG_WARNING, 0,
                        GD_MSG_DICT_GET_FAILED,
                        "Failed to get socket keepalive-time");
        }
        ret = dict_get_int32 (this->options,
                              "transport.tcp-user-timeout",
                              &timeout);
        if (ret) {
                gf_msg ("glusterd", GF_LOG_WARNING, 0,
                        GD_MSG_DICT_GET_FAILED,
                        "Failed to get tcp-user-timeout");
        }

        if ((interval > 0) || (time > 0))
                ret = rpc_transport_keepalive_options_set (dict, interval,
                                                           time, timeout);
        *options = dict;
out:
        gf_msg_debug ("glusterd", 0, "Returning %d", ret);
        return ret;
}

int
glusterd_friend_rpc_create (xlator_t *this, glusterd_peerinfo_t *peerinfo,
                            glusterd_peerctx_args_t *args)
{
        dict_t                   *options = NULL;
        int                       ret     = -1;
        glusterd_peerctx_t       *peerctx = NULL;
        data_t                   *data    = NULL;

        peerctx = GF_CALLOC (1, sizeof (*peerctx), gf_gld_mt_peerctx_t);
        if (!peerctx)
                goto out;

        if (args)
                peerctx->args = *args;

        gf_uuid_copy (peerctx->peerid, peerinfo->uuid);
        peerctx->peername = gf_strdup (peerinfo->hostname);
        peerctx->peerinfo_gen = peerinfo->generation; /* A peerinfos generation
                                                         number can be used to
                                                         uniquely identify a
                                                         peerinfo */

        ret = glusterd_transport_inet_options_build (&options,
                                                     peerinfo->hostname,
                                                     peerinfo->port);
        if (ret)
                goto out;

        /*
         * For simulated multi-node testing, we need to make sure that we
         * create our RPC endpoint with the same address that the peer would
         * use to reach us.
         */
        if (this->options) {
                data = dict_get(this->options,"transport.socket.bind-address");
                if (data) {
                        ret = dict_set(options,
                                       "transport.socket.source-addr",data);
                }
                data = dict_get(this->options,"ping-timeout");
                if (data) {
                        ret = dict_set(options,
                                       "ping-timeout",data);
                }
        }

        /* Enable encryption for the client connection if management encryption
         * is enabled
         */
        if (this->ctx->secure_mgmt) {
                ret = dict_set_str (options, "transport.socket.ssl-enabled",
                                    "on");
                if (ret) {
                        gf_msg ("glusterd", GF_LOG_ERROR, 0,
                                GD_MSG_DICT_SET_FAILED,
                                "failed to set ssl-enabled in dict");
                        goto out;
                }
        }

        ret = glusterd_rpc_create (&peerinfo->rpc, options,
                                   glusterd_peer_rpc_notify, peerctx,
                                   _gf_false);
        if (ret) {
                gf_msg (this->name, GF_LOG_ERROR, 0,
                        GD_MSG_RPC_CREATE_FAIL,
                        "failed to create rpc for"
                        " peer %s", peerinfo->hostname);
                gf_event (EVENT_PEER_RPC_CREATE_FAILED, "peer=%s",
                          peerinfo->hostname);
                goto out;
        }
        peerctx = NULL;
        ret = 0;
out:
        GF_FREE (peerctx);
        return ret;
}

int
glusterd_friend_add (const char *hoststr, int port,
                     glusterd_friend_sm_state_t state,
                     uuid_t *uuid,
                     glusterd_peerinfo_t **friend,
                     gf_boolean_t restore,
                     glusterd_peerctx_args_t *args)
{
        int                     ret = 0;
        xlator_t               *this = NULL;
        glusterd_conf_t        *conf = NULL;

        this = THIS;
        conf = this->private;
        GF_ASSERT (conf);
        GF_ASSERT (hoststr);
        GF_ASSERT (friend);

        *friend = glusterd_peerinfo_new (state, uuid, hoststr, port);
        if (*friend == NULL) {
                ret = -1;
                goto out;
        }

        /*
         * We can't add to the list after calling glusterd_friend_rpc_create,
         * even if it succeeds, because by then the callback to take it back
         * off and free might have happened already (notably in the case of an
         * invalid peer name).  That would mean we're adding something that had
         * just been free, and we're likely to crash later.
         */
        cds_list_add_tail_rcu (&(*friend)->uuid_list, &conf->peers);

        //restore needs to first create the list of peers, then create rpcs
        //to keep track of quorum in race-free manner. In restore for each peer
        //rpc-create calls rpc_notify when the friend-list is partially
        //constructed, leading to wrong quorum calculations.
        if (!restore) {
                ret = glusterd_store_peerinfo (*friend);
                if (ret == 0) {
                        ret = glusterd_friend_rpc_create (this, *friend, args);
                }
                else {
                        gf_msg (this->name, GF_LOG_ERROR, 0,
                                GD_MSG_PEERINFO_CREATE_FAIL,
                                "Failed to store peerinfo");
                        gf_event (EVENT_PEER_STORE_FAILURE, "peer=%s",
                                  (*friend)->hostname);
                }
        }

        if (ret) {
                (void) glusterd_peerinfo_cleanup (*friend);
                *friend = NULL;
        }

out:
        gf_msg (this->name, GF_LOG_INFO, 0,
                GD_MSG_CONNECT_RETURNED, "connect returned %d", ret);
        return ret;
}

/* glusterd_friend_add_from_peerinfo() adds a new peer into the local friends
 * list from a pre created @peerinfo object. It otherwise works similarly to
 * glusterd_friend_add()
 */
int
glusterd_friend_add_from_peerinfo (glusterd_peerinfo_t *friend,
                                   gf_boolean_t restore,
                                   glusterd_peerctx_args_t *args)
{
        int                     ret = 0;
        xlator_t               *this = NULL;
        glusterd_conf_t        *conf = NULL;

        this = THIS;
        conf = this->private;
        GF_ASSERT (conf);

        GF_VALIDATE_OR_GOTO (this->name, (friend != NULL), out);

        /*
         * We can't add to the list after calling glusterd_friend_rpc_create,
         * even if it succeeds, because by then the callback to take it back
         * off and free might have happened already (notably in the case of an
         * invalid peer name).  That would mean we're adding something that had
         * just been free, and we're likely to crash later.
         */
        cds_list_add_tail_rcu (&friend->uuid_list, &conf->peers);

        //restore needs to first create the list of peers, then create rpcs
        //to keep track of quorum in race-free manner. In restore for each peer
        //rpc-create calls rpc_notify when the friend-list is partially
        //constructed, leading to wrong quorum calculations.
        if (!restore) {
                ret = glusterd_store_peerinfo (friend);
                if (ret == 0) {
                        ret = glusterd_friend_rpc_create (this, friend, args);
                }
                else {
                        gf_msg (this->name, GF_LOG_ERROR, 0,
                                GD_MSG_PEERINFO_CREATE_FAIL,
                                "Failed to store peerinfo");
                        gf_event (EVENT_PEER_STORE_FAILURE, "peer=%s",
                                  friend->hostname);
                }
        }

out:
        gf_msg (this->name, GF_LOG_INFO, 0,
                GD_MSG_CONNECT_RETURNED,
                "connect returned %d", ret);
        return ret;
}

int
glusterd_probe_begin (rpcsvc_request_t *req, const char *hoststr, int port,
                      dict_t *dict, int *op_errno)
{
        int                             ret = -1;
        glusterd_peerinfo_t             *peerinfo = NULL;
        glusterd_peerctx_args_t         args = {0};
        glusterd_friend_sm_event_t      *event = NULL;

        GF_ASSERT (hoststr);

        rcu_read_lock ();
        peerinfo = glusterd_peerinfo_find (NULL, hoststr);

        if (peerinfo == NULL) {
                gf_msg ("glusterd", GF_LOG_INFO, 0,
                        GD_MSG_PEER_NOT_FOUND, "Unable to find peerinfo"
                        " for host: %s (%d)", hoststr, port);
                args.mode = GD_MODE_ON;
                args.req  = req;
                args.dict = dict;
                ret = glusterd_friend_add (hoststr, port,
                                           GD_FRIEND_STATE_DEFAULT,
                                           NULL, &peerinfo, 0, &args);
                if ((!ret) && (!peerinfo->connected)) {
                        ret = GLUSTERD_CONNECTION_AWAITED;
                }

        } else if (peerinfo->connected &&
                   (GD_FRIEND_STATE_BEFRIENDED == peerinfo->state.state)) {
                if (peerinfo->detaching) {
                        ret = -1;
                        if (op_errno)
                                *op_errno = GF_PROBE_FRIEND_DETACHING;
                        goto out;
                }
                ret = glusterd_peer_hostname_update (peerinfo, hoststr,
                                                     _gf_false);
                if (ret)
                        goto out;
                // Injecting a NEW_NAME event to update cluster
                ret = glusterd_friend_sm_new_event (GD_FRIEND_EVENT_NEW_NAME,
                                                    &event);
                if (!ret) {
                        event->peername = gf_strdup (peerinfo->hostname);
                        gf_uuid_copy (event->peerid, peerinfo->uuid);

                        ret = glusterd_friend_sm_inject_event (event);
                        glusterd_xfer_cli_probe_resp (req, 0, GF_PROBE_SUCCESS,
                                                      NULL, (char*)hoststr,
                                                      port, dict);
                }
        } else {
                glusterd_xfer_cli_probe_resp (req, 0, GF_PROBE_FRIEND, NULL,
                                              (char*)hoststr, port, dict);
                ret = 0;
        }

out:
        rcu_read_unlock ();
        gf_msg_debug ("glusterd", 0, "returning %d", ret);
        return ret;
}

int
glusterd_deprobe_begin (rpcsvc_request_t *req, const char *hoststr, int port,
                        uuid_t uuid, dict_t *dict, int *op_errno)
{
        int                             ret = -1;
        glusterd_peerinfo_t             *peerinfo = NULL;
        glusterd_friend_sm_event_t      *event = NULL;
        glusterd_probe_ctx_t            *ctx = NULL;

        GF_ASSERT (hoststr);
        GF_ASSERT (req);

        rcu_read_lock ();

        peerinfo = glusterd_peerinfo_find (uuid, hoststr);
        if (peerinfo == NULL) {
                ret = -1;
                gf_msg ("glusterd", GF_LOG_INFO, 0,
                        GD_MSG_PEER_NOT_FOUND, "Unable to find peerinfo"
                        " for host: %s %d", hoststr, port);
                goto out;
        }

        if (!peerinfo->rpc) {
                //handle this case
                goto out;
        }

        if (peerinfo->detaching) {
                ret = -1;
                if (op_errno)
                        *op_errno = GF_DEPROBE_FRIEND_DETACHING;
                goto out;
        }

        ret = glusterd_friend_sm_new_event
                (GD_FRIEND_EVENT_INIT_REMOVE_FRIEND, &event);

        if (ret) {
                gf_msg ("glusterd", GF_LOG_ERROR, 0,
                        GD_MSG_EVENT_NEW_GET_FAIL,
                        "Unable to get new event");
                goto out;
        }

        ctx = GF_CALLOC (1, sizeof(*ctx), gf_gld_mt_probe_ctx_t);

        if (!ctx) {
                goto out;
        }

        ctx->hostname = gf_strdup (hoststr);
        ctx->port = port;
        ctx->req = req;
        ctx->dict = dict;

        event->ctx = ctx;

        event->peername = gf_strdup (hoststr);
        gf_uuid_copy (event->peerid, uuid);

        ret = glusterd_friend_sm_inject_event (event);

        if (ret) {
                gf_msg ("glusterd", GF_LOG_ERROR, 0,
                        GD_MSG_EVENT_INJECT_FAIL, "Unable to inject event %d, "
                        "ret = %d", event->event, ret);
                goto out;
        }
        peerinfo->detaching = _gf_true;

out:
        rcu_read_unlock ();
        return ret;
}


int
glusterd_xfer_friend_remove_resp (rpcsvc_request_t *req, char *hostname, int port)
{
        gd1_mgmt_friend_rsp  rsp = {{0}, };
        int32_t              ret = -1;
        xlator_t             *this = NULL;

        GF_ASSERT (hostname);

        rsp.op_ret = 0;
        this = THIS;
        GF_ASSERT (this);

        gf_uuid_copy (rsp.uuid, MY_UUID);
        rsp.hostname = hostname;
        rsp.port = port;
        ret = glusterd_submit_reply (req, &rsp, NULL, 0, NULL,
                                     (xdrproc_t)xdr_gd1_mgmt_friend_rsp);

        gf_msg ("glusterd", GF_LOG_INFO, 0,
                GD_MSG_RESPONSE_INFO,
                "Responded to %s (%d), ret: %d", hostname, port, ret);
        return ret;
}


int
glusterd_xfer_friend_add_resp (rpcsvc_request_t *req, char *myhostname,
                               char *remote_hostname, int port, int32_t op_ret,
                               int32_t op_errno)
{
        gd1_mgmt_friend_rsp  rsp = {{0}, };
        int32_t              ret = -1;
        xlator_t             *this = NULL;

        GF_ASSERT (myhostname);

        this = THIS;
        GF_ASSERT (this);

        gf_uuid_copy (rsp.uuid, MY_UUID);
        rsp.op_ret = op_ret;
        rsp.op_errno = op_errno;
        rsp.hostname = gf_strdup (myhostname);
        rsp.port = port;

        ret = glusterd_submit_reply (req, &rsp, NULL, 0, NULL,
                                     (xdrproc_t)xdr_gd1_mgmt_friend_rsp);

        gf_msg ("glusterd", GF_LOG_INFO, 0,
                GD_MSG_RESPONSE_INFO,
                "Responded to %s (%d), ret: %d, op_ret: %d", remote_hostname,
                port, ret, op_ret);
        GF_FREE (rsp.hostname);
        return ret;
}

static void
set_probe_error_str (int op_ret, int op_errno, char *op_errstr, char *errstr,
                     size_t len, char *hostname, int port)
{
        if ((op_errstr) && (strcmp (op_errstr, ""))) {
                snprintf (errstr, len, "%s", op_errstr);
                return;
        }

        if (!op_ret) {
                switch (op_errno) {
                        case GF_PROBE_LOCALHOST:
                                snprintf (errstr, len, "Probe on localhost not "
                                          "needed");
                                break;

                        case GF_PROBE_FRIEND:
                                snprintf (errstr, len, "Host %s port %d already"
                                          " in peer list", hostname, port);
                                break;

                        case GF_PROBE_FRIEND_DETACHING:
                                snprintf (errstr, len, "Peer is already being "
                                          "detached from cluster.\n"
                                          "Check peer status by running "
                                          "gluster peer status");
                                break;
                        default:
                                if (op_errno != 0)
                                        snprintf (errstr, len, "Probe returned "
                                                  "with %s",
                                                  strerror (op_errno));
                                break;
                }
        } else {
                switch (op_errno) {
                        case GF_PROBE_ANOTHER_CLUSTER:
                                snprintf (errstr, len, "%s is either already "
                                          "part of another cluster or having "
                                          "volumes configured", hostname);
                                break;

                        case GF_PROBE_VOLUME_CONFLICT:
                                snprintf (errstr, len, "Atleast one volume on "
                                          "%s conflicts with existing volumes "
                                          "in the cluster", hostname);
                                break;

                        case GF_PROBE_UNKNOWN_PEER:
                                snprintf (errstr, len, "%s responded with "
                                          "'unknown peer' error, this could "
                                          "happen if %s doesn't have localhost "
                                          "in its peer database", hostname,
                                          hostname);
                                break;

                        case GF_PROBE_ADD_FAILED:
                                snprintf (errstr, len, "Failed to add peer "
                                          "information on %s", hostname);
                                break;

                        case GF_PROBE_SAME_UUID:
                                snprintf (errstr, len, "Peer uuid (host %s) is "
                                          "same as local uuid", hostname);
                                break;

                        case GF_PROBE_QUORUM_NOT_MET:
                                snprintf (errstr, len, "Cluster quorum is not "
                                          "met. Changing peers is not allowed "
                                          "in this state");
                                break;

                        case GF_PROBE_MISSED_SNAP_CONFLICT:
                                snprintf (errstr, len, "Failed to update "
                                          "list of missed snapshots from "
                                          "peer %s", hostname);
                                break;

                        case GF_PROBE_SNAP_CONFLICT:
                                snprintf (errstr, len, "Conflict in comparing "
                                          "list of snapshots from "
                                          "peer %s", hostname);
                                break;

                        default:
                                snprintf (errstr, len, "Probe returned with "
                                          "%s", strerror (op_errno));
                                break;
                }
        }
}

int
glusterd_xfer_cli_probe_resp (rpcsvc_request_t *req, int32_t op_ret,
                              int32_t op_errno, char *op_errstr, char *hostname,
                              int port, dict_t *dict)
{
        gf_cli_rsp           rsp = {0,};
        int32_t              ret = -1;
        char        errstr[2048] = {0,};
        char            *cmd_str = NULL;
        xlator_t           *this = THIS;

        GF_ASSERT (req);
        GF_ASSERT (this);

        (void) set_probe_error_str (op_ret, op_errno, op_errstr, errstr,
                                    sizeof (errstr), hostname, port);

        if (dict) {
                ret = dict_get_str (dict, "cmd-str", &cmd_str);
                if (ret)
                        gf_msg (this->name, GF_LOG_ERROR, 0,
                                GD_MSG_CMDSTR_NOTFOUND_IN_DICT, "Failed to get "
                                "command string");
        }

        rsp.op_ret = op_ret;
        rsp.op_errno = op_errno;
        rsp.op_errstr = (errstr[0] != '\0') ? errstr : "";

        gf_cmd_log ("", "%s : %s %s %s", cmd_str,
                    (op_ret) ? "FAILED" : "SUCCESS",
                    (errstr[0] != '\0') ? ":" : " ",
                    (errstr[0] != '\0') ? errstr : " ");

        ret = glusterd_submit_reply (req, &rsp, NULL, 0, NULL,
                                     (xdrproc_t)xdr_gf_cli_rsp);

        if (dict)
                dict_unref (dict);
        gf_msg_debug (this->name, 0, "Responded to CLI, ret: %d", ret);

        return ret;
}

static void
set_deprobe_error_str (int op_ret, int op_errno, char *op_errstr, char *errstr,
                       size_t len, char *hostname)
{
        if ((op_errstr) && (strcmp (op_errstr, ""))) {
                snprintf (errstr, len, "%s", op_errstr);
                return;
        }

        if (op_ret) {
                switch (op_errno) {
                        case GF_DEPROBE_LOCALHOST:
                                snprintf (errstr, len, "%s is localhost",
                                hostname);
                                break;

                        case GF_DEPROBE_NOT_FRIEND:
                                snprintf (errstr, len, "%s is not part of "
                                          "cluster", hostname);
                                break;

                        case GF_DEPROBE_BRICK_EXIST:
                                snprintf (errstr, len, "Brick(s) with the peer "
                                          "%s exist in cluster", hostname);
                                break;

                        case GF_DEPROBE_FRIEND_DOWN:
                                snprintf (errstr, len, "One of the peers is "
                                          "probably down. Check with "
                                          "'peer status'");
                                break;

                        case GF_DEPROBE_QUORUM_NOT_MET:
                                snprintf (errstr, len, "Cluster quorum is not "
                                          "met. Changing peers is not allowed "
                                          "in this state");
                                break;

                        case GF_DEPROBE_FRIEND_DETACHING:
                                snprintf (errstr, len, "Peer is already being "
                                          "detached from cluster.\n"
                                          "Check peer status by running "
                                          "gluster peer status");
                                break;
                        default:
                                snprintf (errstr, len, "Detach returned with "
                                          "%s", strerror (op_errno));
                                break;

                }
        }
}


int
glusterd_xfer_cli_deprobe_resp (rpcsvc_request_t *req, int32_t op_ret,
                                int32_t op_errno, char *op_errstr,
                                char *hostname, dict_t *dict)
{
        gf_cli_rsp             rsp = {0,};
        int32_t                ret = -1;
        char              *cmd_str = NULL;
        char          errstr[2048] = {0,};

        GF_ASSERT (req);

        (void) set_deprobe_error_str (op_ret, op_errno, op_errstr, errstr,
                                      sizeof (errstr), hostname);

        if (dict) {
                ret = dict_get_str (dict, "cmd-str", &cmd_str);
                if (ret)
                        gf_msg (THIS->name, GF_LOG_ERROR, 0,
                                GD_MSG_CMDSTR_NOTFOUND_IN_DICT, "Failed to get "
                                "command string");
        }

        rsp.op_ret = op_ret;
        rsp.op_errno = op_errno;
        rsp.op_errstr = (errstr[0] != '\0') ? errstr : "";

        gf_cmd_log ("", "%s : %s %s %s", cmd_str,
                    (op_ret) ? "FAILED" : "SUCCESS",
                    (errstr[0] != '\0') ? ":" : " ",
                    (errstr[0] != '\0') ? errstr : " ");

        ret = glusterd_submit_reply (req, &rsp, NULL, 0, NULL,
                                     (xdrproc_t)xdr_gf_cli_rsp);

        gf_msg_debug (THIS->name, 0, "Responded to CLI, ret: %d", ret);

        return ret;
}

int32_t
glusterd_list_friends (rpcsvc_request_t *req, dict_t *dict, int32_t flags)
{
        int32_t                 ret = -1;
        glusterd_conf_t         *priv = NULL;
        glusterd_peerinfo_t     *entry = NULL;
        int32_t                 count = 0;
        dict_t                  *friends = NULL;
        gf1_cli_peer_list_rsp   rsp = {0,};
        char                    my_uuid_str[64] = {0,};
        char                    key[256] = {0,};

        priv = THIS->private;
        GF_ASSERT (priv);

        friends = dict_new ();
        if (!friends) {
                gf_msg (THIS->name, GF_LOG_ERROR, ENOMEM,
                        GD_MSG_NO_MEMORY, "Out of Memory");
                goto out;
        }

        /* Reset ret to 0, needed to prevent failure incase no peers exist */
        ret = 0;
        rcu_read_lock ();
        if (!cds_list_empty (&priv->peers)) {
                cds_list_for_each_entry_rcu (entry, &priv->peers, uuid_list) {
                        count++;
                        ret = gd_add_peer_detail_to_dict (entry,
                                                                friends, count);
                        if (ret)
                                goto unlock;
                }
        }
unlock:
        rcu_read_unlock ();
        if (ret)
                goto out;

        if (flags == GF_CLI_LIST_POOL_NODES) {
                count++;
                snprintf (key, 256, "friend%d.uuid", count);
                uuid_utoa_r (MY_UUID, my_uuid_str);
                ret = dict_set_str (friends, key, my_uuid_str);
                if (ret)
                        goto out;

                snprintf (key, 256, "friend%d.hostname", count);
                ret = dict_set_str (friends, key, "localhost");
                if (ret)
                        goto out;

                snprintf (key, 256, "friend%d.connected", count);
                ret = dict_set_int32 (friends, key, 1);
                if (ret)
                        goto out;
        }

        ret = dict_set_int32 (friends, "count", count);
        if (ret)
                goto out;

        ret = dict_allocate_and_serialize (friends, &rsp.friends.friends_val,
                                           &rsp.friends.friends_len);

        if (ret)
                goto out;

        ret = 0;
out:

        if (friends)
                dict_unref (friends);

        rsp.op_ret = ret;

        glusterd_submit_reply (req, &rsp, NULL, 0, NULL,
                               (xdrproc_t)xdr_gf1_cli_peer_list_rsp);
        ret = 0;
        GF_FREE (rsp.friends.friends_val);

        return ret;
}

int32_t
glusterd_get_volumes (rpcsvc_request_t *req, dict_t *dict, int32_t flags)
{
        int32_t                 ret = -1;
        int32_t                 ret_bkp = 0;
        glusterd_conf_t         *priv = NULL;
        glusterd_volinfo_t      *entry = NULL;
        int32_t                 count = 0;
        dict_t                  *volumes = NULL;
        gf_cli_rsp              rsp = {0,};
        char                    *volname = NULL;

        priv = THIS->private;
        GF_ASSERT (priv);
        volumes = dict_new ();
        if (!volumes) {
                gf_msg ("glusterd", GF_LOG_ERROR, ENOMEM,
                        GD_MSG_NO_MEMORY, "Out of Memory");
                goto out;
        }

        if (cds_list_empty (&priv->volumes)) {
                if (flags == GF_CLI_GET_VOLUME)
                        ret_bkp = -1;
                ret = 0;
                goto respond;
        }
        if (flags == GF_CLI_GET_VOLUME_ALL) {
                cds_list_for_each_entry (entry, &priv->volumes, vol_list) {
                        ret = glusterd_add_volume_detail_to_dict (entry,
                                                        volumes, count);
                        if (ret)
                                goto respond;

                        count++;

                }

        } else if (flags == GF_CLI_GET_NEXT_VOLUME) {
                ret = dict_get_str (dict, "volname", &volname);

                if (ret) {
                        if (priv->volumes.next) {
                                entry = cds_list_entry (priv->volumes.next,
                                                        typeof (*entry),
                                                        vol_list);
                        }
                } else {
                        ret = glusterd_volinfo_find (volname, &entry);
                        if (ret)
                                goto respond;
                        entry = cds_list_entry (entry->vol_list.next,
                                                typeof (*entry),
                                                vol_list);
                }

                if (&entry->vol_list == &priv->volumes) {
                       goto respond;
                } else {
                        ret = glusterd_add_volume_detail_to_dict (entry,
                                                         volumes, count);
                        if (ret)
                                goto respond;

                        count++;
                }
        } else if (flags == GF_CLI_GET_VOLUME) {
                ret = dict_get_str (dict, "volname", &volname);

                if (ret)
                        goto respond;

                ret = glusterd_volinfo_find (volname, &entry);
                if (ret) {
                        ret_bkp = ret;
                        goto respond;
                }

                ret = glusterd_add_volume_detail_to_dict (entry,
                                                 volumes, count);
                if (ret)
                        goto respond;

                count++;
        }

respond:
        ret = dict_set_int32 (volumes, "count", count);
        if (ret)
                goto out;
        ret = dict_allocate_and_serialize (volumes, &rsp.dict.dict_val,
                                           &rsp.dict.dict_len);

        if (ret)
                goto out;

        ret = 0;
out:
        if (ret_bkp == -1) {
                rsp.op_ret = ret_bkp;
                rsp.op_errstr = "Volume does not exist";
                rsp.op_errno = EG_NOVOL;
        } else {
                rsp.op_ret = ret;
                rsp.op_errstr = "";
        }
        glusterd_submit_reply (req, &rsp, NULL, 0, NULL,
                               (xdrproc_t)xdr_gf_cli_rsp);
        ret = 0;

        if (volumes)
                dict_unref (volumes);

        GF_FREE (rsp.dict.dict_val);
        return ret;
}

int
__glusterd_handle_status_volume (rpcsvc_request_t *req)
{
        int32_t                         ret     = -1;
        uint32_t                        cmd     = 0;
        dict_t                         *dict    = NULL;
        char                           *volname = 0;
        gf_cli_req                      cli_req = {{0,}};
        glusterd_op_t                   cli_op  = GD_OP_STATUS_VOLUME;
        char                            err_str[2048] = {0,};
        xlator_t                       *this = NULL;
        glusterd_conf_t                *conf = NULL;

        GF_ASSERT (req);
        this = THIS;
        GF_ASSERT (this);
        conf = this->private;
        GF_ASSERT (conf);

        ret = xdr_to_generic (req->msg[0], &cli_req, (xdrproc_t)xdr_gf_cli_req);
        if (ret < 0) {
                //failed to decode msg;
                gf_msg (this->name, GF_LOG_ERROR, 0,
                        GD_MSG_REQ_DECODE_FAIL, "Failed to decode "
                        "request received from cli");
                req->rpc_err = GARBAGE_ARGS;
                goto out;
        }

        if (cli_req.dict.dict_len > 0) {
                dict = dict_new();
                if (!dict)
                        goto out;
                ret = dict_unserialize (cli_req.dict.dict_val,
                                        cli_req.dict.dict_len, &dict);
                if (ret < 0) {
                        gf_msg (this->name, GF_LOG_ERROR, 0,
                                GD_MSG_DICT_UNSERIALIZE_FAIL, "failed to "
                                "unserialize buffer");
                        snprintf (err_str, sizeof (err_str), "Unable to decode "
                                  "the command");
                        goto out;
                }

        }

        ret = dict_get_uint32 (dict, "cmd", &cmd);
        if (ret)
                goto out;

        if (!(cmd & GF_CLI_STATUS_ALL)) {
                ret = dict_get_str (dict, "volname", &volname);
                if (ret) {
                        snprintf (err_str, sizeof (err_str), "Unable to get "
                                  "volume name");
                        gf_msg (this->name, GF_LOG_ERROR, 0,
                                GD_MSG_VOL_NOT_FOUND, "%s", err_str);
                        goto out;
                }
                gf_msg (this->name, GF_LOG_INFO, 0,
                        GD_MSG_STATUS_VOL_REQ_RCVD,
                        "Received status volume req for volume %s", volname);

        }
        if ((cmd & GF_CLI_STATUS_QUOTAD) &&
            (conf->op_version == GD_OP_VERSION_MIN)) {
                snprintf (err_str, sizeof (err_str), "The cluster is operating "
                          "at version 1. Getting the status of quotad is not "
                          "allowed in this state.");
                ret = -1;
                goto out;
        }

        if ((cmd & GF_CLI_STATUS_SNAPD) &&
            (conf->op_version < GD_OP_VERSION_3_6_0)) {
                snprintf (err_str, sizeof (err_str), "The cluster is operating "
                          "at a lesser version than %d. Getting the status of "
                          "snapd is not allowed in this state",
                          GD_OP_VERSION_3_6_0);
                ret = -1;
                goto out;
        }

        if ((cmd & GF_CLI_STATUS_BITD) &&
            (conf->op_version < GD_OP_VERSION_3_7_0)) {
                snprintf (err_str, sizeof (err_str), "The cluster is operating "
                          "at a lesser version than %d. Getting the status of "
                          "bitd is not allowed in this state",
                          GD_OP_VERSION_3_7_0);
                ret = -1;
                goto out;
        }

        if ((cmd & GF_CLI_STATUS_TIERD) &&
            (conf->op_version < GD_OP_VERSION_3_10_0)) {
                snprintf (err_str, sizeof (err_str), "The cluster is operating "
                          "at a lesser version than %d. Getting the status of "
                          "tierd is not allowed in this state",
                          GD_OP_VERSION_3_6_0);
                ret = -1;
                goto out;
        }

        if ((cmd & GF_CLI_STATUS_SCRUB) &&
            (conf->op_version < GD_OP_VERSION_3_7_0)) {
                snprintf (err_str, sizeof (err_str), "The cluster is operating "
                          "at a lesser version than %d. Getting the status of "
                          "scrub is not allowed in this state",
                          GD_OP_VERSION_3_7_0);
                ret = -1;
                goto out;
        }

        ret = glusterd_op_begin_synctask (req, GD_OP_STATUS_VOLUME, dict);

out:

        if (ret) {
                if (err_str[0] == '\0')
                        snprintf (err_str, sizeof (err_str),
                                  "Operation failed");
                ret = glusterd_op_send_cli_response (cli_op, ret, 0, req,
                                                     dict, err_str);
        }
        free (cli_req.dict.dict_val);

        return ret;
}

int
glusterd_handle_status_volume (rpcsvc_request_t *req)
{
        return glusterd_big_locked_handler (req,
                                            __glusterd_handle_status_volume);
}

int
__glusterd_handle_cli_clearlocks_volume (rpcsvc_request_t *req)
{
        int32_t                         ret = -1;
        gf_cli_req                      cli_req = {{0,}};
        glusterd_op_t                   cli_op = GD_OP_CLEARLOCKS_VOLUME;
        char                            *volname = NULL;
        dict_t                          *dict = NULL;
        char                            err_str[2048] = {0,};
        xlator_t                        *this = NULL;

        GF_ASSERT (req);
        this = THIS;
        GF_ASSERT (this);

        ret = -1;
        ret = xdr_to_generic (req->msg[0], &cli_req, (xdrproc_t)xdr_gf_cli_req);
        if (ret < 0) {
                gf_msg (this->name, GF_LOG_ERROR, 0,
                        GD_MSG_REQ_DECODE_FAIL, "Failed to decode "
                        "request received from cli");
                req->rpc_err = GARBAGE_ARGS;
                goto out;
        }

        if (cli_req.dict.dict_len) {
                dict  = dict_new ();

                ret = dict_unserialize (cli_req.dict.dict_val,
                                        cli_req.dict.dict_len,
                                        &dict);
                if (ret < 0) {
                        gf_msg (this->name, GF_LOG_ERROR, 0,
                                GD_MSG_DICT_UNSERIALIZE_FAIL,
                                "failed to unserialize req-buffer to"
                                " dictionary");
                        snprintf (err_str, sizeof (err_str), "unable to decode "
                                  "the command");
                        goto out;
                }

        } else {
                ret = -1;
                gf_msg (this->name, GF_LOG_ERROR, 0,
                        GD_MSG_CLI_REQ_EMPTY, "Empty cli request.");
                goto out;
        }

        ret = dict_get_str (dict, "volname", &volname);
        if (ret) {
                snprintf (err_str, sizeof (err_str), "Unable to get volume "
                          "name");
                gf_msg (this->name, GF_LOG_ERROR, 0,
                        GD_MSG_VOLNAME_NOTFOUND_IN_DICT, "%s", err_str);
                goto out;
        }

        gf_msg (this->name, GF_LOG_INFO, 0,
                GD_MSG_CLRCLK_VOL_REQ_RCVD, "Received clear-locks volume req "
                "for volume %s", volname);

        ret = glusterd_op_begin_synctask (req, GD_OP_CLEARLOCKS_VOLUME, dict);

out:
        if (ret) {
                if (err_str[0] == '\0')
                        snprintf (err_str, sizeof (err_str),
                                  "Operation failed");
                ret = glusterd_op_send_cli_response (cli_op, ret, 0, req,
                                                     dict, err_str);
        }
        free (cli_req.dict.dict_val);

        return ret;
}

int
glusterd_handle_cli_clearlocks_volume (rpcsvc_request_t *req)
{
        return glusterd_big_locked_handler (req,
                                            __glusterd_handle_cli_clearlocks_volume);
}

static int
get_volinfo_from_brickid (char *brickid, glusterd_volinfo_t **volinfo)
{
        int             ret         = -1;
        char           *volid_str  = NULL;
        char           *brick      = NULL;
        char           *brickid_dup = NULL;
        uuid_t          volid       = {0};
        xlator_t       *this        = NULL;

        this = THIS;
        GF_ASSERT (this);
        GF_ASSERT (brickid);

        brickid_dup = gf_strdup (brickid);
        if (!brickid_dup)
                goto out;

        volid_str = brickid_dup;
        brick = strchr (brickid_dup, ':');
        if (!brick) {
                gf_msg (this->name, GF_LOG_ERROR, 0,
                        GD_MSG_BRICK_NOT_FOUND,
                        "Invalid brickid");
                goto out;
        }

        *brick = '\0';
        brick++;
        gf_uuid_parse (volid_str, volid);
        ret = glusterd_volinfo_find_by_volume_id (volid, volinfo);
        if (ret) {
                /* Check if it is a snapshot volume */
                ret = glusterd_snap_volinfo_find_by_volume_id (volid, volinfo);
                if (ret) {
                        gf_msg (this->name, GF_LOG_WARNING, 0,
                                GD_MSG_VOLINFO_GET_FAIL,
                                "Failed to find volinfo");
                        goto out;
                }
        }

        ret = 0;
out:
        GF_FREE (brickid_dup);
        return ret;
}

static int
__glusterd_handle_barrier (rpcsvc_request_t *req)
{
        int          ret     = -1;
        xlator_t     *this   = NULL;
        gf_cli_req   cli_req = {{0,}};
        dict_t       *dict   = NULL;
        char *volname = NULL;

        GF_ASSERT (req);
        this = THIS;
        GF_ASSERT(this);

        ret = xdr_to_generic (req->msg[0], &cli_req, (xdrproc_t)xdr_gf_cli_req);
        if (ret < 0) {
                gf_msg (this->name, GF_LOG_ERROR, 0,
                        GD_MSG_REQ_DECODE_FAIL, "Failed to decode "
                        "request received from cli");
                req->rpc_err = GARBAGE_ARGS;
                goto out;
        }

        if (!cli_req.dict.dict_len) {
                ret = -1;
                goto out;
        }

        dict = dict_new();
        if (!dict) {
                ret = -1;
                goto out;
        }
        ret = dict_unserialize (cli_req.dict.dict_val, cli_req.dict.dict_len,
                                &dict);
        if (ret < 0) {
                gf_msg (this->name, GF_LOG_ERROR, 0,
                        GD_MSG_DICT_UNSERIALIZE_FAIL, "Failed to unserialize "
                        "request dictionary.");
                goto out;
        }

        ret = dict_get_str (dict, "volname", &volname);
        if (ret) {
                gf_msg (this->name, GF_LOG_ERROR, 0,
                        GD_MSG_VOLNAME_NOTFOUND_IN_DICT,
                        "Volname not present in "
                        "dict");
                goto out;
        }
        gf_msg (this->name, GF_LOG_INFO, 0,
                GD_MSG_BARRIER_VOL_REQ_RCVD,
                "Received barrier volume request for "
                "volume %s", volname);

        ret = glusterd_op_begin_synctask (req, GD_OP_BARRIER, dict);

out:
        if (ret) {
                ret = glusterd_op_send_cli_response (GD_OP_BARRIER, ret, 0, req,
                                                     dict, "Operation failed");
        }
        free (cli_req.dict.dict_val);
        return ret;
}

int
glusterd_handle_barrier (rpcsvc_request_t *req)
{
        return glusterd_big_locked_handler (req, __glusterd_handle_barrier);
}

static gf_boolean_t
gd_is_global_option (char *opt_key)
{
        GF_VALIDATE_OR_GOTO (THIS->name, opt_key, out);

        return (strcmp (opt_key, GLUSTERD_SHARED_STORAGE_KEY) == 0 ||
                strcmp (opt_key, GLUSTERD_QUORUM_RATIO_KEY) == 0 ||
                strcmp (opt_key, GLUSTERD_GLOBAL_OP_VERSION_KEY) == 0 ||
                strcmp (opt_key, GLUSTERD_BRICK_MULTIPLEX_KEY) == 0 ||
                strcmp (opt_key, GLUSTERD_MAX_OP_VERSION_KEY) == 0);

out:
        return _gf_false;
}

int32_t
glusterd_get_volume_opts (rpcsvc_request_t *req, dict_t *dict)
{
        int32_t                   ret = -1;
        int32_t                   count = 1;
        int                       exists = 0;
        char                      *key = NULL;
        char                      *orig_key = NULL;
        char                      *key_fixed = NULL;
        char                      *volname = NULL;
        char                      *value = NULL;
        char                      err_str[2048] = {0,};
        char                      warn_str[2048] = {0,};
        char                      dict_key[50] = {0,};
        xlator_t                  *this = NULL;
        glusterd_conf_t           *priv = NULL;
        glusterd_volinfo_t        *volinfo = NULL;
        gf_cli_rsp                rsp = {0,};
        char                      op_version_buff[10] = {0,};

        this = THIS;
        GF_ASSERT (this);

        priv = this->private;
        GF_ASSERT (priv);

        GF_ASSERT (req);
        GF_ASSERT (dict);

        ret = dict_get_str (dict, "volname", &volname);
        if (ret) {
                snprintf (err_str, sizeof (err_str), "Failed to get volume "
                          "name while handling get volume option command");
                gf_msg (this->name, GF_LOG_ERROR, 0,
                        GD_MSG_VOLNAME_NOTFOUND_IN_DICT, "%s", err_str);
                goto out;
        }

        if (strcasecmp (volname, "all") == 0) {
                ret = glusterd_get_global_options_for_all_vols (req, dict,
                                                                &rsp.op_errstr);
                goto out;
        }


        ret = dict_get_str (dict, "key", &key);
        if (ret) {
                snprintf (err_str, sizeof (err_str), "Failed to get key "
                          "while handling get volume option for %s", volname);
                gf_msg (this->name, GF_LOG_ERROR, 0,
                        GD_MSG_DICT_GET_FAILED, "%s", err_str);
                goto out;
        }
        gf_msg_debug (this->name, 0, "Received get volume opt request for "
                "volume %s", volname);

        ret = glusterd_volinfo_find (volname, &volinfo);
        if (ret) {
                snprintf (err_str, sizeof(err_str),
                          FMTSTR_CHECK_VOL_EXISTS, volname);
                gf_msg (this->name, GF_LOG_ERROR, 0,
                        GD_MSG_VOL_NOT_FOUND, FMTSTR_CHECK_VOL_EXISTS,
                        volname);
                goto out;
        }
        if (strcmp(key, "all")) {
                if (fnmatch (GD_HOOKS_SPECIFIC_KEY, key, FNM_NOESCAPE) == 0) {
                        sprintf (dict_key, "key%d", count);
                        ret = dict_set_str(dict, dict_key, key);
                        if (ret) {
                                gf_msg (this->name, GF_LOG_ERROR, 0,
                                        GD_MSG_DICT_SET_FAILED, "Failed to "
                                        "set %s in dictionary", key);
                                goto out;
                        }
                        sprintf (dict_key, "value%d", count);
                        ret = dict_get_str (volinfo->dict, key, &value);
                        if (ret) {
                                gf_msg (this->name, GF_LOG_ERROR, 0,
                                        GD_MSG_DICT_GET_FAILED, "Failed to "
                                        "get %s in dictionary", key);
                                goto out;
                        }
                        ret = dict_set_str(dict, dict_key, value);
                        if (ret) {
                                gf_msg (this->name, GF_LOG_ERROR, 0,
                                        GD_MSG_DICT_SET_FAILED, "Failed to "
                                        "set %s in dictionary", key);
                                goto out;
                        }
                } else {
                        exists = glusterd_check_option_exists (key, &key_fixed);
                        if (!exists) {
                                snprintf (err_str, sizeof (err_str), "Option "
                                          "with name: %s does not exist", key);
                                gf_msg (this->name, GF_LOG_ERROR, EINVAL,
                                        GD_MSG_UNKNOWN_KEY, "%s",
                                        err_str);
                                if (key_fixed)
                                        snprintf (err_str + ret,
                                                  sizeof (err_str) - ret,
                                                  "Did you mean %s?",
                                                  key_fixed);
                                ret = -1;
                                goto out;
                        }
                        if (key_fixed) {
                                orig_key = key;
                                key = key_fixed;
                        }

                        if (gd_is_global_option (key)) {
                                snprintf (warn_str, sizeof (warn_str),
                                          "Warning: Support to get "
                                          "global option value using "
                                          "`volume get <volname>` will be "
                                          "deprecated from next release. "
                                          "Consider using `volume get all` "
                                          "instead for global options");

                                ret = dict_set_str (dict, "warning", warn_str);
                                if (ret) {
                                        gf_msg (this->name, GF_LOG_ERROR,
                                                0, GD_MSG_DICT_SET_FAILED,
                                                "Failed to set warning "
                                                "message in dictionary");
                                        goto out;
                                }
                        }

                        if (strcmp (key, "cluster.max-op-version") == 0) {
                                ret = glusterd_get_global_max_op_version (req, dict, 1);
                                if (ret)
                                        goto out;
                        } else if (strcmp (key, "cluster.op-version") == 0) {
                                sprintf (dict_key, "key%d", count);
                                ret = dict_set_str(dict, dict_key, key);
                                if (ret) {
                                        gf_msg (this->name, GF_LOG_ERROR, 0,
                                                GD_MSG_DICT_SET_FAILED, "Failed"
                                                "to set %s in dictionary", key);
                                        goto out;
                                }
                                sprintf (dict_key, "value%d", count);
                                sprintf (op_version_buff, "%d",
                                         priv->op_version);
                                ret = dict_set_str (dict, dict_key,
                                                    op_version_buff);
                                if (ret) {
                                        gf_msg (this->name, GF_LOG_ERROR, 0,
                                                GD_MSG_DICT_SET_FAILED, "Failed"
                                                " to set value for key %s in "
                                                "dictionary", key);
                                        goto out;
                                }
                        } else if (strcmp (key,
                                         "config.memory-accounting") == 0) {
                                sprintf (dict_key, "key%d", count);
                                ret = dict_set_str(dict, dict_key, key);
                                if (ret) {
                                        gf_msg (this->name, GF_LOG_ERROR, 0,
                                                GD_MSG_DICT_SET_FAILED, "Failed"
                                                " to set %s in dictionary",
                                                key);
                                        goto out;
                                }
                                sprintf (dict_key, "value%d", count);

                                if (volinfo->memory_accounting)
                                        ret = dict_set_str(dict, dict_key,
                                                           "Enabled");
                                else
                                        ret = dict_set_str(dict, dict_key,
                                                           "Disabled");
                                if (ret) {
                                        gf_msg (this->name, GF_LOG_ERROR, 0,
                                                GD_MSG_DICT_SET_FAILED, "Failed"
                                                " to set value for key %s in "
                                                "dictionary", key);
                                        goto out;
                                }
                        } else if (strcmp (key, "config.transport") == 0) {
                                sprintf (dict_key, "key%d", count);
                                ret = dict_set_str(dict, dict_key, key);
                                if (ret) {
                                        gf_msg (this->name, GF_LOG_ERROR, 0,
                                                GD_MSG_DICT_SET_FAILED, "Failed"
                                                "to set %s in dictionary", key);
                                        goto out;
                                }
                                sprintf (dict_key, "value%d", count);

                                if (volinfo->transport_type
                                                == GF_TRANSPORT_RDMA)
                                        ret = dict_set_str(dict, dict_key,
                                                           "rdma");
                                else if (volinfo->transport_type
                                                == GF_TRANSPORT_TCP)
                                        ret = dict_set_str(dict, dict_key,
                                                           "tcp");
                                else if (volinfo->transport_type ==
                                         GF_TRANSPORT_BOTH_TCP_RDMA)
                                        ret = dict_set_str(dict, dict_key,
                                                           "tcp,rdma");
                                else
                                        ret = dict_set_str(dict, dict_key,
                                                           "none");

                                if (ret) {
                                        gf_msg (this->name, GF_LOG_ERROR, 0,
                                                GD_MSG_DICT_SET_FAILED, "Failed"
                                                " to set value for key %s in "
                                                "dictionary", key);
                                        goto out;
                                }
                        } else {
                                sprintf (dict_key, "key%d", count);
                                ret = dict_set_str(dict, dict_key, key);
                                if (ret) {
                                        gf_msg (this->name, GF_LOG_ERROR, 0,
                                                GD_MSG_DICT_SET_FAILED, "Failed"
                                                " to set %s in dictionary",
                                                key);
                                        goto out;
                                }
                                sprintf (dict_key, "value%d", count);
                                ret = dict_get_str (priv->opts, key, &value);
                                if (!ret) {
                                        ret = dict_set_str(dict, dict_key,
                                                           value);
                                        if (ret) {
                                                gf_msg (this->name,
                                                        GF_LOG_ERROR, 0,
                                                        GD_MSG_DICT_SET_FAILED,
                                                        "Failed to set %s in "
                                                        " dictionary", key);
                                                goto out;
                                        }
                                } else {
                                        ret = glusterd_get_default_val_for_volopt
                                                               (dict,
                                                                _gf_false,
                                                                key, orig_key,
                                                                volinfo,
                                                                &rsp.op_errstr);
                                        if (ret && !rsp.op_errstr) {
                                                snprintf (err_str,
                                                          sizeof(err_str),
                                                          "Failed to fetch the "
                                                          "value of %s, check "
                                                          "log file for more"
                                                          " details", key);
                                        }
                                }
                        }
                }
                /* Request is for a single option, explicitly set count to 1
                 * in the dictionary.
                 */
                ret = dict_set_int32 (dict, "count", 1);
                if (ret) {
                        gf_msg (this->name, GF_LOG_ERROR, errno,
                                GD_MSG_DICT_SET_FAILED, "Failed to set count "
                                "value in the dictionary");
                        goto out;
                }
        } else {
                /* Handle the "all" volume option request */
                ret = glusterd_get_default_val_for_volopt (dict, _gf_true, NULL,
                                                           NULL, volinfo,
                                                           &rsp.op_errstr);
                if (ret && !rsp.op_errstr) {
                        snprintf (err_str, sizeof(err_str),
                                  "Failed to fetch the value of all volume "
                                  "options, check log file for more details");
                }

        }

out:
        if (ret) {
                if (!rsp.op_errstr)
                        rsp.op_errstr = err_str;
                rsp.op_ret =  ret;
        }
        else {
                rsp.op_errstr = "";
                rsp.op_ret = 0;
        }

        ret = dict_allocate_and_serialize (dict, &rsp.dict.dict_val,
                                           &rsp.dict.dict_len);

        glusterd_submit_reply (req, &rsp, NULL, 0, NULL,
                               (xdrproc_t)xdr_gf_cli_rsp);
        return ret;
}

int
__glusterd_handle_get_vol_opt (rpcsvc_request_t *req)
{
        int32_t                         ret = -1;
        gf_cli_req                      cli_req = {{0,}};
        dict_t                          *dict = NULL;
        char                            err_str[2048] = {0,};
        xlator_t                        *this = NULL;

        this = THIS;
        GF_ASSERT (this);

        GF_ASSERT (req);

        ret = xdr_to_generic (req->msg[0], &cli_req, (xdrproc_t)xdr_gf_cli_req);
        if (ret < 0) {
                snprintf (err_str, sizeof (err_str), "Failed to decode "
                          "request received from cli");
                gf_msg (this->name, GF_LOG_ERROR, 0,
                        GD_MSG_REQ_DECODE_FAIL, "%s", err_str);
                req->rpc_err = GARBAGE_ARGS;
                goto out;
        }

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
                        snprintf (err_str, sizeof (err_str), "Unable to decode "
                                  "the command");
                        goto out;
                } else {
                        dict->extra_stdfree = cli_req.dict.dict_val;
                }
        }
        ret = glusterd_get_volume_opts (req, dict);

out:
        if (dict)
                dict_unref (dict);

        return ret;
}

int
glusterd_handle_get_vol_opt (rpcsvc_request_t *req)
{
        return glusterd_big_locked_handler (req, __glusterd_handle_get_vol_opt);
}

static int
glusterd_print_global_options (dict_t *opts, char *key, data_t *val, void *data)
{
        FILE *fp = NULL;

        GF_VALIDATE_OR_GOTO (THIS->name, key, out);
        GF_VALIDATE_OR_GOTO (THIS->name, val, out);
        GF_VALIDATE_OR_GOTO (THIS->name, data, out);

        if (strcmp (key, "global-option-version") == 0)
                goto out;

        fp = (FILE *) data;
        fprintf (fp, "%s: %s\n", key, val->data);
out:
        return 0;
}

static int
glusterd_print_volume_options (dict_t *opts, char *key, data_t *val, void *data)
{
        FILE *fp = NULL;

        GF_VALIDATE_OR_GOTO (THIS->name, key, out);
        GF_VALIDATE_OR_GOTO (THIS->name, val, out);
        GF_VALIDATE_OR_GOTO (THIS->name, data, out);

        fp = (FILE *) data;
        fprintf (fp, "Volume%d.options.%s: %s\n", volcount, key, val->data);
out:
        return 0;
}

static int
glusterd_print_snapinfo_by_vol (FILE *fp, glusterd_volinfo_t *volinfo, int volcount)
{
        int                     ret = -1;
        glusterd_volinfo_t      *snap_vol = NULL;
        glusterd_volinfo_t      *tmp_vol = NULL;
        glusterd_snap_t         *snapinfo = NULL;
        int                     snapcount = 0;
        char                    timestr[64] = {0,};
        char                    snap_status_str[STATUS_STRLEN] = {0,};

        GF_VALIDATE_OR_GOTO (THIS->name, volinfo, out);
        GF_VALIDATE_OR_GOTO (THIS->name, fp, out);

        cds_list_for_each_entry_safe (snap_vol, tmp_vol, &volinfo->snap_volumes,
                                      snapvol_list) {
                snapcount++;
                snapinfo = snap_vol->snapshot;

                ret = glusterd_get_snap_status_str (snapinfo, snap_status_str);
                if (ret) {
                        gf_msg (THIS->name, GF_LOG_ERROR, 0,
                                GD_MSG_STATE_STR_GET_FAILED,
                                "Failed to get status for snapshot: %s",
                                snapinfo->snapname);

                        goto out;
                }
                gf_time_fmt (timestr, sizeof timestr, snapinfo->time_stamp,
                             gf_timefmt_FT);

                fprintf (fp, "Volume%d.snapshot%d.name: %s\n",
                         volcount, snapcount, snapinfo->snapname);
                fprintf (fp, "Volume%d.snapshot%d.id: %s\n", volcount, snapcount,
                         gf_strdup (uuid_utoa (snapinfo->snap_id)));
                fprintf (fp, "Volume%d.snapshot%d.time: %s\n",
                         volcount, snapcount, timestr);

                if (snapinfo->description)
                        fprintf (fp, "Volume%d.snapshot%d.description: %s\n",
                         volcount, snapcount, snapinfo->description);
                fprintf (fp, "Volume%d.snapshot%d.status: %s\n",
                         volcount, snapcount, snap_status_str);
        }

        ret = 0;
out:
        return ret;
}

static int
glusterd_get_state (rpcsvc_request_t *req, dict_t *dict)
{
        int32_t                      ret = -1;
        gf_cli_rsp                   rsp = {0,};
        FILE                        *fp = NULL;
        DIR                         *dp = NULL;
        char                         err_str[2048] = {0,};
        glusterd_conf_t             *priv = NULL;
        glusterd_peerinfo_t         *peerinfo = NULL;
        glusterd_peer_hostname_t    *peer_hostname_info = NULL;
        glusterd_volinfo_t          *volinfo = NULL;
        glusterd_brickinfo_t        *brickinfo = NULL;
        xlator_t                    *this = NULL;
        char                        *odir = NULL;
        char                        *filename = NULL;
        char                        *ofilepath = NULL;
        int                          count = 0;
        int                          count_bkp = 0;
        int                          odirlen = 0;
        time_t                       now = 0;
        char                         timestamp[16] = {0,};

        char    *vol_type_str = NULL;
        char    *hot_tier_type_str = NULL;
        char    *cold_tier_type_str = NULL;

        char     transport_type_str[STATUS_STRLEN] = {0,};
        char     quorum_status_str[STATUS_STRLEN] = {0,};
        char     rebal_status_str[STATUS_STRLEN] = {0,};
        char     vol_status_str[STATUS_STRLEN] = {0,};

        this = THIS;
        GF_VALIDATE_OR_GOTO (THIS->name, this, out);

        priv = THIS->private;
        GF_VALIDATE_OR_GOTO (this->name, priv, out);

        GF_VALIDATE_OR_GOTO (this->name, dict, out);

        ret = dict_get_str (dict, "odir", &odir);
        if (ret) {
                gf_asprintf (&odir, "%s", "/var/run/gluster/");
                gf_msg (this->name, GF_LOG_INFO, 0,
                        GD_MSG_DICT_GET_FAILED,
                        "Default output directory: %s", odir);
        }

        dp = sys_opendir (odir);
        if (dp) {
                sys_closedir (dp);
        } else {
                if (errno == ENOENT) {
                        snprintf (err_str, sizeof (err_str),
                                  "Output directory %s does not exist.", odir);
                        gf_msg (this->name, GF_LOG_ERROR, 0,
                                GD_MSG_DICT_GET_FAILED, "%s", err_str);
                } else if (errno == ENOTDIR) {
                        snprintf (err_str, sizeof (err_str), "Output directory "
                                  "does not exist. %s points to a file.", odir);
                        gf_msg (this->name, GF_LOG_ERROR, 0,
                                GD_MSG_DICT_GET_FAILED, "%s", err_str);
                }

                ret = -1;
                goto out;
        }

        ret = dict_get_str (dict, "filename", &filename);
        if (ret) {
                now = time (NULL);
                strftime (timestamp, sizeof (timestamp),
                          "%Y%m%d_%H%M%S", localtime (&now));
                gf_asprintf (&filename, "%s_%s", "glusterd_state", timestamp);

                gf_msg (this->name, GF_LOG_INFO, 0,
                        GD_MSG_DICT_GET_FAILED,
                        "Default filename: %s", filename);
        }

        odirlen = strlen (odir);
        if (odir[odirlen-1] != '/')
                strcat (odir, "/");

        gf_asprintf (&ofilepath, "%s%s", odir, filename);

        ret = dict_set_str (dict, "ofilepath", ofilepath);
        if (ret) {
                gf_msg (this->name, GF_LOG_ERROR, 0,
                        GD_MSG_DICT_SET_FAILED, "Unable to set output path");
                goto out;
        }

        fp = fopen (ofilepath, "w");
        if (!fp) {
                snprintf (err_str, sizeof (err_str),
                          "Failed to open file at %s", ofilepath);
                gf_msg (this->name, GF_LOG_ERROR, 0,
                        GD_MSG_DICT_GET_FAILED, "%s", err_str);
                ret = -1;
                goto out;
        }

        fprintf (fp, "[Global]\n");

        fprintf (fp, "MYUUID: %s\n", gf_strdup (uuid_utoa (priv->uuid)));
        fprintf (fp, "op-version: %d\n", priv->op_version);

        fprintf (fp, "\n[Global options]\n");

        if (priv->opts)
                dict_foreach (priv->opts, glusterd_print_global_options, fp);

        rcu_read_lock ();
        fprintf (fp, "\n[Peers]\n");

        cds_list_for_each_entry_rcu (peerinfo, &priv->peers, uuid_list) {
                fprintf (fp, "Peer%d.primary_hostname: %s\n", ++count,
                         peerinfo->hostname);
                fprintf (fp, "Peer%d.uuid: %s\n", count, gd_peer_uuid_str (peerinfo));
                fprintf (fp, "Peer%d.state: %s\n", count,
                         glusterd_friend_sm_state_name_get (peerinfo->state.state));
                fprintf (fp, "Peer%d.connected: %s\n", count,
                         peerinfo->connected ? "Connected" : "Disconnected");

                fprintf (fp, "Peer%d.othernames: ", count);
                count_bkp = 0;
                cds_list_for_each_entry (peer_hostname_info,
                                         &peerinfo->hostnames, hostname_list) {
                        if (strcmp (peerinfo->hostname,
                                    peer_hostname_info->hostname) == 0)
                                continue;

                        if (count_bkp > 0)
                                fprintf (fp, ",");

                        fprintf (fp, "%s", peer_hostname_info->hostname);
                        count_bkp++;
                }
                count_bkp = 0;
                fprintf (fp, "\n");
        }
        rcu_read_unlock ();

        count = 0;
        fprintf (fp, "\n[Volumes]\n");

        cds_list_for_each_entry (volinfo, &priv->volumes, vol_list) {
                ret = glusterd_volume_get_type_str (volinfo, &vol_type_str);
                if (ret) {
                        gf_msg (this->name, GF_LOG_ERROR, 0,
                                GD_MSG_STATE_STR_GET_FAILED,
                                "Failed to get type for volume: %s",
                                volinfo->volname);
                        goto out;
                }

                ret = glusterd_volume_get_status_str (volinfo, vol_status_str);
                if (ret) {
                        gf_msg (this->name, GF_LOG_ERROR, 0,
                                GD_MSG_STATE_STR_GET_FAILED,
                                "Failed to get status for volume: %s",
                                volinfo->volname);
                        goto out;
                }

                ret = glusterd_volume_get_transport_type_str (volinfo,
                                                              transport_type_str);
                if (ret) {
                        gf_msg (this->name, GF_LOG_ERROR, 0,
                                GD_MSG_STATE_STR_GET_FAILED,
                                "Failed to get transport type for volume: %s",
                                volinfo->volname);
                        goto out;
                }

                ret = glusterd_volume_get_quorum_status_str (volinfo,
                                                             quorum_status_str);
                if (ret) {
                        gf_msg (this->name, GF_LOG_ERROR, 0,
                                GD_MSG_STATE_STR_GET_FAILED,
                                "Failed to get quorum status for volume: %s",
                                volinfo->volname);
                        goto out;
                }

                ret = glusterd_volume_get_rebalance_status_str (volinfo,
                                                                rebal_status_str);
                if (ret) {
                        gf_msg (this->name, GF_LOG_ERROR, 0,
                                GD_MSG_STATE_STR_GET_FAILED,
                                "Failed to get rebalance status for volume: %s",
                                volinfo->volname);
                        goto out;
                }

                fprintf (fp, "Volume%d.name: %s\n", ++count, volinfo->volname);
                fprintf (fp, "Volume%d.id: %s\n", count,
                         gf_strdup (uuid_utoa (volinfo->volume_id)));
                fprintf (fp, "Volume%d.type: %s\n", count, vol_type_str);
                fprintf (fp, "Volume%d.transport_type: %s\n", count,
                         transport_type_str);
                fprintf (fp, "Volume%d.status: %s\n", count, vol_status_str);
                fprintf (fp, "Volume%d.brickcount: %d\n", count,
                         volinfo->brick_count);

                count_bkp = count;
                count = 0;
                cds_list_for_each_entry (brickinfo, &volinfo->bricks, brick_list) {
                        fprintf (fp, "Volume%d.Brick%d.path: %s:%s\n",
                                 count_bkp, ++count, brickinfo->hostname,
                                 brickinfo->path);
                        fprintf (fp, "Volume%d.Brick%d.hostname: %s\n",
                                 count_bkp, count, brickinfo->hostname);

                        /* Add following information only for bricks
                         *  local to current node */
                        if (gf_uuid_compare (brickinfo->uuid, MY_UUID))
                                continue;
                        fprintf (fp, "Volume%d.Brick%d.port: %d\n", count_bkp,
                                 count, brickinfo->port);
                        fprintf (fp, "Volume%d.Brick%d.rdma_port: %d\n", count_bkp,
                                 count, brickinfo->rdma_port);
                        fprintf (fp, "Volume%d.Brick%d.status: %s\n", count_bkp,
                                 count, brickinfo->status ? "Started" : "Stopped");

                        /*FIXME: This is a hacky way of figuring out whether a
                         * brick belongs to the hot or cold tier */
                        if (volinfo->type == GF_CLUSTER_TYPE_TIER) {
                                 fprintf (fp, "Volume%d.Brick%d.tier: %s\n",
                                      count_bkp, count,
                                      count <= volinfo->tier_info.hot_brick_count ?
                                      "Hot" : "Cold");
                        }
                }

                count = count_bkp;

                ret = glusterd_print_snapinfo_by_vol (fp, volinfo, count);
                if (ret)
                        goto out;

                fprintf (fp, "Volume%d.snap_count: %"PRIu64"\n", count,
                         volinfo->snap_count);
                fprintf (fp, "Volume%d.stripe_count: %d\n", count,
                         volinfo->stripe_count);
                fprintf (fp, "Volume%d.replica_count: %d\n", count,
                         volinfo->replica_count);
                fprintf (fp, "Volume%d.subvol_count: %d\n", count,
                         volinfo->subvol_count);
                fprintf (fp, "Volume%d.arbiter_count: %d\n", count,
                         volinfo->arbiter_count);
                fprintf (fp, "Volume%d.disperse_count: %d\n", count,
                         volinfo->disperse_count);
                fprintf (fp, "Volume%d.redundancy_count: %d\n", count,
                         volinfo->redundancy_count);
                fprintf (fp, "Volume%d.quorum_status: %s\n", count,
                                quorum_status_str);

                fprintf (fp, "Volume%d.snapd_svc.online_status: %s\n", count,
                         volinfo->snapd.svc.online ? "Online" : "Offline");
                fprintf (fp, "Volume%d.snapd_svc.inited: %s\n", count,
                         volinfo->snapd.svc.inited ? "True" : "False");

                fprintf (fp, "Volume%d.rebalance.id: %s\n", count,
                         gf_strdup (uuid_utoa (volinfo->rebal.rebalance_id)));
                fprintf (fp, "Volume%d.rebalance.status: %s\n", count,
                         rebal_status_str);
                fprintf (fp, "Volume%d.rebalance.failures: %"PRIu64"\n", count,
                         volinfo->rebal.rebalance_failures);
                fprintf (fp, "Volume%d.rebalance.skipped: %"PRIu64"\n", count,
                         volinfo->rebal.skipped_files);
                fprintf (fp, "Volume%d.rebalance.lookedup: %"PRIu64"\n", count,
                         volinfo->rebal.lookedup_files);
                fprintf (fp, "Volume%d.rebalance.files: %"PRIu64"\n", count,
                         volinfo->rebal.rebalance_files);
                fprintf (fp, "Volume%d.rebalance.data: %s\n", count,
                         gf_uint64_2human_readable (volinfo->rebal.rebalance_data));

                if (volinfo->type == GF_CLUSTER_TYPE_TIER) {
                        ret = glusterd_volume_get_hot_tier_type_str (
                                                volinfo, &hot_tier_type_str);
                        if (ret) {
                                gf_msg (this->name, GF_LOG_ERROR, 0,
                                        GD_MSG_STATE_STR_GET_FAILED,
                                        "Failed to get hot tier type for "
                                        "volume: %s", volinfo->volname);
                                goto out;
                        }

                        ret = glusterd_volume_get_cold_tier_type_str (
                                                volinfo, &cold_tier_type_str);
                        if (ret) {
                               gf_msg (this->name, GF_LOG_ERROR, 0,
                                        GD_MSG_STATE_STR_GET_FAILED,
                                        "Failed to get cold tier type for "
                                        "volume: %s", volinfo->volname);
                               goto out;
                        }

                        fprintf (fp, "Volume%d.tier_info.cold_tier_type: %s\n",
                                 count, cold_tier_type_str);
                        fprintf (fp, "Volume%d.tier_info.cold_brick_count: %d\n",
                                 count, volinfo->tier_info.cold_brick_count);
                        fprintf (fp, "Volume%d.tier_info.cold_replica_count: %d\n",
                                 count, volinfo->tier_info.cold_replica_count);
                        fprintf (fp, "Volume%d.tier_info.cold_disperse_count: %d\n",
                                 count, volinfo->tier_info.cold_disperse_count);
                        fprintf (fp, "Volume%d.tier_info.cold_dist_leaf_count: %d\n",
                                 count, volinfo->tier_info.cold_dist_leaf_count);
                        fprintf (fp, "Volume%d.tier_info.cold_redundancy_count: %d\n",
                                 count, volinfo->tier_info.cold_redundancy_count);
                        fprintf (fp, "Volume%d.tier_info.hot_tier_type: %s\n",
                                 count, hot_tier_type_str);
                        fprintf (fp, "Volume%d.tier_info.hot_brick_count: %d\n",
                                 count, volinfo->tier_info.hot_brick_count);
                        fprintf (fp, "Volume%d.tier_info.hot_replica_count: %d\n",
                                 count, volinfo->tier_info.hot_replica_count);
                        fprintf (fp, "Volume%d.tier_info.promoted: %d\n",
                                 count, volinfo->tier_info.promoted);
                        fprintf (fp, "Volume%d.tier_info.demoted: %d\n",
                                 count, volinfo->tier_info.demoted);
                }

                if (volinfo->rep_brick.src_brick && volinfo->rep_brick.dst_brick) {
                        fprintf (fp, "Volume%d.replace_brick.src: %s:%s\n", count,
                                 volinfo->rep_brick.src_brick->hostname,
                                 volinfo->rep_brick.src_brick->path);
                        fprintf (fp, "Volume%d.replace_brick.dest: %s:%s\n", count,
                                 volinfo->rep_brick.dst_brick->hostname,
                                 volinfo->rep_brick.dst_brick->path);
                }

                volcount = count;
                if (volinfo->dict)
                        dict_foreach (volinfo->dict,
                                      glusterd_print_volume_options, fp);

                fprintf (fp, "\n");
        }

        count = 0;

        fprintf (fp, "\n[Services]\n");

        if (priv->shd_svc.inited) {
                fprintf (fp, "svc%d.name: %s\n", ++count, priv->shd_svc.name);
                fprintf (fp, "svc%d.online_status: %s\n\n", count,
                         priv->shd_svc.online ? "Online" : "Offline");
        }

        if (priv->nfs_svc.inited) {
                fprintf (fp, "svc%d.name: %s\n", ++count, priv->nfs_svc.name);
                fprintf (fp, "svc%d.online_status: %s\n\n", count,
                         priv->nfs_svc.online ? "Online" : "Offline");
        }

        if (priv->bitd_svc.inited) {
                fprintf (fp, "svc%d.name: %s\n", ++count, priv->bitd_svc.name);
                fprintf (fp, "svc%d.online_status: %s\n\n", count,
                         priv->bitd_svc.online ? "Online" : "Offline");
        }

        if (priv->scrub_svc.inited) {
                fprintf (fp, "svc%d.name: %s\n", ++count, priv->scrub_svc.name);
                fprintf (fp, "svc%d.online_status: %s\n\n", count,
                         priv->scrub_svc.online ? "Online" : "Offline");
        }

        if (priv->quotad_svc.inited) {
                fprintf (fp, "svc%d.name: %s\n", ++count, priv->quotad_svc.name);
                fprintf (fp, "svc%d.online_status: %s\n\n", count,
                         priv->quotad_svc.online ? "Online" : "Offline");
        }

        fprintf (fp, "\n[Misc]\n");
        if (priv->pmap) {
                fprintf (fp, "Base port: %d\n", priv->pmap->base_port);
                fprintf (fp, "Last allocated port: %d\n",
                         priv->pmap->last_alloc);
        }
out:

        if (fp)
                fclose(fp);

        rsp.op_ret = ret;
        rsp.op_errstr = err_str;

        ret = dict_allocate_and_serialize (dict, &rsp.dict.dict_val,
                                           &rsp.dict.dict_len);
        glusterd_to_cli (req, &rsp, NULL, 0, NULL,
                         (xdrproc_t)xdr_gf_cli_rsp, dict);

        return ret;
}

static int
__glusterd_handle_get_state (rpcsvc_request_t *req)
{
        int32_t                         ret = -1;
        gf_cli_req                      cli_req = {{0,},};
        dict_t                          *dict = NULL;
        char                            err_str[2048] = {0,};
        xlator_t                        *this = NULL;

        this = THIS;
        GF_VALIDATE_OR_GOTO (THIS->name, this, out);
        GF_VALIDATE_OR_GOTO (this->name, req, out);

        gf_msg (this->name, GF_LOG_INFO, 0, GD_MSG_DAEMON_STATE_REQ_RCVD,
                "Received request to get state for glusterd");

        ret = xdr_to_generic (req->msg[0], &cli_req, (xdrproc_t)xdr_gf_cli_req);
        if (ret < 0) {
                snprintf (err_str, sizeof (err_str), "Failed to decode "
                          "request received from cli");
                gf_msg (this->name, GF_LOG_ERROR, 0,
                        GD_MSG_REQ_DECODE_FAIL, "%s", err_str);
                req->rpc_err = GARBAGE_ARGS;
                goto out;
        }

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
                        snprintf (err_str, sizeof (err_str), "Unable to decode"
                                  " the command");
                        goto out;
                } else {
                        dict->extra_stdfree = cli_req.dict.dict_val;
                }
        }

        ret = glusterd_get_state (req, dict);

out:
        if (dict && ret) {
                /*
                 * When glusterd_to_cli (called from glusterd_get_state)
                 * succeeds, it frees the dict for us, so this would be a
                 * double free, but in other cases it's our responsibility.
                 */
                dict_unref (dict);
        }
        return ret;
}

int
glusterd_handle_get_state (rpcsvc_request_t *req)
{
        return glusterd_big_locked_handler (req,
                                            __glusterd_handle_get_state);
}

static int
get_brickinfo_from_brickid (char *brickid, glusterd_brickinfo_t **brickinfo)
{
        glusterd_volinfo_t      *volinfo    = NULL;
        char                    *volid_str  = NULL;
        char                    *brick      = NULL;
        char                    *brickid_dup = NULL;
        uuid_t                  volid       = {0};
        int                     ret         = -1;

        brickid_dup = gf_strdup (brickid);
        if (!brickid_dup)
                goto out;

        volid_str = brickid_dup;
        brick = strchr (brickid_dup, ':');
        if (!volid_str || !brick)
                goto out;

        *brick = '\0';
        brick++;
        gf_uuid_parse (volid_str, volid);
        ret = glusterd_volinfo_find_by_volume_id (volid, &volinfo);
        if (ret) {
                /* Check if it a snapshot volume */
                ret = glusterd_snap_volinfo_find_by_volume_id (volid, &volinfo);
                if (ret)
                        goto out;
        }

        ret = glusterd_volume_brickinfo_get_by_brick (brick, volinfo,
                                                      brickinfo,
                                                      _gf_false);
        if (ret)
                goto out;

        ret = 0;
out:
        GF_FREE (brickid_dup);
        return ret;
}

int
__glusterd_brick_rpc_notify (struct rpc_clnt *rpc, void *mydata,
                             rpc_clnt_event_t event, void *data)
{
        char                    *brickid           = NULL;
        int                      ret               = 0;
        glusterd_conf_t         *conf              = NULL;
        glusterd_brickinfo_t    *brickinfo         = NULL;
        glusterd_volinfo_t      *volinfo           = NULL;
        xlator_t                *this              = NULL;

        brickid = mydata;
        if (!brickid)
                return 0;

        ret = get_brickinfo_from_brickid (brickid, &brickinfo);
        if (ret)
                return 0;

        this = THIS;
        GF_ASSERT (this);
        conf = this->private;
        GF_ASSERT (conf);

        switch (event) {
        case RPC_CLNT_CONNECT:
                ret = get_volinfo_from_brickid (brickid, &volinfo);
                if (ret) {
                        gf_msg (this->name, GF_LOG_ERROR, 0,
                                GD_MSG_VOLINFO_GET_FAIL,
                                "Failed to get volinfo from "
                                "brickid(%s)", brickid);
                        goto out;
                }
                /* If a node on coming back up, already starts a brick
                 * before the handshake, and the notification comes after
                 * the handshake is done, then we need to check if this
                 * is a restored brick with a snapshot pending. If so, we
                 * need to stop the brick
                 */
                if (brickinfo->snap_status == -1) {
                        gf_msg (this->name, GF_LOG_INFO, 0,
                                GD_MSG_SNAPSHOT_PENDING,
                                "Snapshot is pending on %s:%s. "
                                "Hence not starting the brick",
                                brickinfo->hostname,
                                brickinfo->path);
                        ret = glusterd_brick_stop (volinfo, brickinfo,
                                                   _gf_false);
                        if (ret) {
                                gf_msg (THIS->name, GF_LOG_ERROR, 0,
                                        GD_MSG_BRICK_STOP_FAIL,
                                        "Unable to stop %s:%s",
                                        brickinfo->hostname, brickinfo->path);
                                goto out;
                        }

                        break;
                }
                rpc_clnt_set_connected (&rpc->conn);
                gf_msg_debug (this->name, 0, "Connected to %s:%s",
                        brickinfo->hostname, brickinfo->path);
                glusterd_set_brick_status (brickinfo, GF_BRICK_STARTED);
                gf_event (EVENT_BRICK_CONNECTED, "peer=%s;volume=%s;brick=%s",
                          brickinfo->hostname, volinfo->volname,
                          brickinfo->path);

                ret = default_notify (this, GF_EVENT_CHILD_UP, NULL);

                break;

        case RPC_CLNT_DISCONNECT:
                rpc_clnt_unset_connected (&rpc->conn);
                if (rpc != brickinfo->rpc) {
                        /*
                         * There used to be a bunch of races in the volume
                         * start/stop code that could result in us getting here
                         * and setting the brick status incorrectly.  Many of
                         * those have been fixed or avoided, but just in case
                         * any are still left it doesn't hurt to keep the extra
                         * check and avoid further damage.
                         */
                        gf_log (this->name, GF_LOG_WARNING,
                                "got disconnect from stale rpc on %s",
                                brickinfo->path);
                        break;
                }
                if (glusterd_is_brick_started (brickinfo)) {
                        gf_msg (this->name, GF_LOG_INFO, 0,
                                GD_MSG_BRICK_DISCONNECTED,
                                "Brick %s:%s has disconnected from glusterd.",
                                brickinfo->hostname, brickinfo->path);
                        ret = get_volinfo_from_brickid (brickid, &volinfo);
                        if (ret) {
                                gf_msg (this->name, GF_LOG_ERROR, 0,
                                        GD_MSG_VOLINFO_GET_FAIL,
                                        "Failed to get volinfo from "
                                        "brickid(%s)", brickid);
                                goto out;
                        }
                        gf_event (EVENT_BRICK_DISCONNECTED,
                                  "peer=%s;volume=%s;brick=%s",
                                  brickinfo->hostname, volinfo->volname,
                                  brickinfo->path);
                }

                glusterd_set_brick_status (brickinfo, GF_BRICK_STOPPED);
                break;

        case RPC_CLNT_DESTROY:
                GF_FREE (mydata);
                mydata = NULL;
                break;
        default:
                gf_msg_trace (this->name, 0,
                        "got some other RPC event %d", event);
                break;
        }

out:
        return ret;
}

int
glusterd_brick_rpc_notify (struct rpc_clnt *rpc, void *mydata,
                           rpc_clnt_event_t event, void *data)
{
        return glusterd_big_locked_notify (rpc, mydata, event, data,
                                           __glusterd_brick_rpc_notify);
}

int
glusterd_friend_remove_notify (glusterd_peerctx_t *peerctx, int32_t op_errno)
{
        int                             ret = -1;
        glusterd_friend_sm_event_t      *new_event = NULL;
        glusterd_peerinfo_t             *peerinfo = NULL;
        rpcsvc_request_t                *req = NULL;
        char                            *errstr = NULL;
        dict_t                          *dict = NULL;

        GF_ASSERT (peerctx);

        rcu_read_lock ();
        peerinfo = glusterd_peerinfo_find_by_generation (peerctx->peerinfo_gen);
        if (!peerinfo) {
                gf_msg_debug (THIS->name, 0, "Could not find peer %s(%s). "
                        "Peer could have been deleted.", peerctx->peername,
                        uuid_utoa (peerctx->peerid));
                ret = 0;
                goto out;
        }

        req = peerctx->args.req;
        dict = peerctx->args.dict;
        errstr = peerctx->errstr;

        ret = glusterd_friend_sm_new_event (GD_FRIEND_EVENT_REMOVE_FRIEND,
                                            &new_event);
        if (!ret) {
                if (!req) {
                        gf_msg (THIS->name, GF_LOG_WARNING, 0,
                                GD_MSG_EVENT_NEW_GET_FAIL,
                                "Unable to find the request for responding "
                                "to User (%s)", peerinfo->hostname);
                        goto out;
                }

                glusterd_xfer_cli_probe_resp (req, -1, op_errno, errstr,
                                              peerinfo->hostname,
                                              peerinfo->port, dict);

                new_event->peername = gf_strdup (peerinfo->hostname);
                gf_uuid_copy (new_event->peerid, peerinfo->uuid);
                ret = glusterd_friend_sm_inject_event (new_event);

        } else {
                gf_msg ("glusterd", GF_LOG_ERROR, 0,
                        GD_MSG_EVENT_INJECT_FAIL,
                        "Unable to create event for removing peer %s",
                        peerinfo->hostname);
        }

out:
        rcu_read_unlock ();
        return ret;
}

int
__glusterd_peer_rpc_notify (struct rpc_clnt *rpc, void *mydata,
                            rpc_clnt_event_t event, void *data)
{
        xlator_t             *this        = NULL;
        glusterd_conf_t      *conf        = NULL;
        int                   ret         = 0;
        int32_t               op_errno    = ENOTCONN;
        glusterd_peerinfo_t  *peerinfo    = NULL;
        glusterd_peerctx_t   *peerctx     = NULL;
        gf_boolean_t         quorum_action = _gf_false;
        glusterd_volinfo_t   *volinfo     = NULL;
        uuid_t               uuid;

        peerctx = mydata;
        if (!peerctx)
                return 0;

        this = THIS;
        conf = this->private;

        if (RPC_CLNT_DESTROY == event) {
                GF_FREE (peerctx->errstr);
                GF_FREE (peerctx->peername);
                GF_FREE (peerctx);
                return 0;
        }

        rcu_read_lock ();

        peerinfo = glusterd_peerinfo_find_by_generation (peerctx->peerinfo_gen);
        if (!peerinfo) {
                /* Peerinfo should be available at this point if its a connect
                 * event. Not finding it means that something terrible has
                 * happened. For non-connect event we might end up having a null
                 * peerinfo, so log at debug level.
                 */
                gf_msg (THIS->name, (RPC_CLNT_CONNECT == event) ?
                        GF_LOG_CRITICAL : GF_LOG_DEBUG, ENOENT,
                        GD_MSG_PEER_NOT_FOUND, "Could not find peer "
                        "%s(%s)", peerctx->peername,
                        uuid_utoa (peerctx->peerid));

                if (RPC_CLNT_CONNECT == event) {
                        gf_event (EVENT_PEER_NOT_FOUND, "peer=%s;uuid=%s",
                                  peerctx->peername,
                                  uuid_utoa (peerctx->peerid));
                }
                ret = -1;
                goto out;
        }

        switch (event) {
        case RPC_CLNT_CONNECT:
        {
                rpc_clnt_set_connected (&rpc->conn);
                gf_msg_debug (this->name, 0, "got RPC_CLNT_CONNECT");
                peerinfo->connected = 1;
                peerinfo->quorum_action = _gf_true;
                peerinfo->generation = uatomic_add_return
                                                   (&conf->generation, 1);
                peerctx->peerinfo_gen = peerinfo->generation;
                /* EVENT_PEER_CONNECT will only be sent if peerctx->uuid is not
                 * NULL, otherwise it indicates this RPC_CLNT_CONNECT is from a
                 * peer probe trigger and given we already generate an event for
                 * peer probe this would be unnecessary.
                 */
                if (!gf_uuid_is_null (peerinfo->uuid)) {
                        gf_event (EVENT_PEER_CONNECT, "host=%s;uuid=%s",
                                  peerinfo->hostname,
                                  uuid_utoa (peerinfo->uuid));
                }
                ret = glusterd_peer_dump_version (this, rpc, peerctx);
                if (ret)
                        gf_msg (this->name, GF_LOG_ERROR, 0,
                                GD_MSG_HANDSHAKE_FAILED,
                                "glusterd handshake failed");
                break;
        }

        case RPC_CLNT_DISCONNECT:
        {
                /* If DISCONNECT event is already processed, skip the further
                 * ones
                 */
                if (is_rpc_clnt_disconnected (&rpc->conn))
                        break;

                rpc_clnt_unset_connected (&rpc->conn);
                gf_msg (this->name, GF_LOG_INFO, 0,
                        GD_MSG_PEER_DISCONNECTED,
                        "Peer <%s> (<%s>), in state <%s>, has disconnected "
                        "from glusterd.",
                        peerinfo->hostname, uuid_utoa (peerinfo->uuid),
                        glusterd_friend_sm_state_name_get (peerinfo->state.state));
                gf_event (EVENT_PEER_DISCONNECT, "peer=%s;uuid=%s;state=%s",
                          peerinfo->hostname, uuid_utoa (peerinfo->uuid),
                          glusterd_friend_sm_state_name_get (peerinfo->state.state));

                if (peerinfo->connected) {
                        if (conf->op_version < GD_OP_VERSION_3_6_0) {
                                glusterd_get_lock_owner (&uuid);
                                if (!gf_uuid_is_null (uuid) &&
                                    !gf_uuid_compare (peerinfo->uuid, uuid))
                                        glusterd_unlock (peerinfo->uuid);
                        } else {
                                cds_list_for_each_entry (volinfo,
                                                         &conf->volumes,
                                                         vol_list) {
                                        ret = glusterd_mgmt_v3_unlock
                                                    (volinfo->volname,
                                                     peerinfo->uuid,
                                                     "vol");
                                        if (ret)
                                                gf_msg (this->name,
                                                        GF_LOG_WARNING, 0,
                                                        GD_MSG_MGMTV3_UNLOCK_FAIL,
                                                        "Lock not released "
                                                        "for %s",
                                                         volinfo->volname);
                                }
                        }

                        op_errno = GF_PROBE_ANOTHER_CLUSTER;
                        ret = 0;
                }

                if ((peerinfo->quorum_contrib != QUORUM_DOWN) &&
                    (peerinfo->state.state == GD_FRIEND_STATE_BEFRIENDED)) {
                        peerinfo->quorum_contrib = QUORUM_DOWN;
                        quorum_action = _gf_true;
                        peerinfo->quorum_action = _gf_false;
                }

                /* Remove peer if it is not a friend and connection/handshake
                *  fails, and notify cli. Happens only during probe.
                */
                if (peerinfo->state.state == GD_FRIEND_STATE_DEFAULT) {
                        glusterd_friend_remove_notify (peerctx, op_errno);
                        goto out;
                }

                peerinfo->connected = 0;
                break;
        }

        default:
                gf_msg_trace (this->name, 0,
                        "got some other RPC event %d", event);
                ret = 0;
                break;
        }

out:
        rcu_read_unlock ();

        glusterd_friend_sm ();
        glusterd_op_sm ();
        if (quorum_action)
                glusterd_do_quorum_action ();
        return ret;
}

int
glusterd_peer_rpc_notify (struct rpc_clnt *rpc, void *mydata,
                          rpc_clnt_event_t event, void *data)
{
        return glusterd_big_locked_notify (rpc, mydata, event, data,
                                           __glusterd_peer_rpc_notify);
}

int
glusterd_null (rpcsvc_request_t *req)
{

        return 0;
}

rpcsvc_actor_t gd_svc_mgmt_actors[GLUSTERD_MGMT_MAXVALUE] = {
        [GLUSTERD_MGMT_NULL]           = { "NULL",           GLUSTERD_MGMT_NULL,           glusterd_null,                  NULL, 0, DRC_NA},
        [GLUSTERD_MGMT_CLUSTER_LOCK]   = { "CLUSTER_LOCK",   GLUSTERD_MGMT_CLUSTER_LOCK,   glusterd_handle_cluster_lock,   NULL, 0, DRC_NA},
        [GLUSTERD_MGMT_CLUSTER_UNLOCK] = { "CLUSTER_UNLOCK", GLUSTERD_MGMT_CLUSTER_UNLOCK, glusterd_handle_cluster_unlock, NULL, 0, DRC_NA},
        [GLUSTERD_MGMT_STAGE_OP]       = { "STAGE_OP",       GLUSTERD_MGMT_STAGE_OP,       glusterd_handle_stage_op,       NULL, 0, DRC_NA},
        [GLUSTERD_MGMT_COMMIT_OP]      = { "COMMIT_OP",      GLUSTERD_MGMT_COMMIT_OP,      glusterd_handle_commit_op,      NULL, 0, DRC_NA},
};

struct rpcsvc_program gd_svc_mgmt_prog = {
        .progname  = "GlusterD svc mgmt",
        .prognum   = GD_MGMT_PROGRAM,
        .progver   = GD_MGMT_VERSION,
        .numactors = GLUSTERD_MGMT_MAXVALUE,
        .actors    = gd_svc_mgmt_actors,
	.synctask  = _gf_true,
};

rpcsvc_actor_t gd_svc_peer_actors[GLUSTERD_FRIEND_MAXVALUE] = {
        [GLUSTERD_FRIEND_NULL]    = { "NULL",          GLUSTERD_MGMT_NULL,     glusterd_null,                         NULL, 0, DRC_NA},
        [GLUSTERD_PROBE_QUERY]    = { "PROBE_QUERY",   GLUSTERD_PROBE_QUERY,   glusterd_handle_probe_query,           NULL, 0, DRC_NA},
        [GLUSTERD_FRIEND_ADD]     = { "FRIEND_ADD",    GLUSTERD_FRIEND_ADD,    glusterd_handle_incoming_friend_req,   NULL, 0, DRC_NA},
        [GLUSTERD_FRIEND_REMOVE]  = { "FRIEND_REMOVE", GLUSTERD_FRIEND_REMOVE, glusterd_handle_incoming_unfriend_req, NULL, 0, DRC_NA},
        [GLUSTERD_FRIEND_UPDATE]  = { "FRIEND_UPDATE", GLUSTERD_FRIEND_UPDATE, glusterd_handle_friend_update,         NULL, 0, DRC_NA},
};

struct rpcsvc_program gd_svc_peer_prog = {
        .progname  = "GlusterD svc peer",
        .prognum   = GD_FRIEND_PROGRAM,
        .progver   = GD_FRIEND_VERSION,
        .numactors = GLUSTERD_FRIEND_MAXVALUE,
        .actors    = gd_svc_peer_actors,
	.synctask  = _gf_false,
};



rpcsvc_actor_t gd_svc_cli_actors[GLUSTER_CLI_MAXVALUE] = {
        [GLUSTER_CLI_PROBE]              = { "CLI_PROBE",         GLUSTER_CLI_PROBE,            glusterd_handle_cli_probe,             NULL, 0, DRC_NA},
        [GLUSTER_CLI_CREATE_VOLUME]      = { "CLI_CREATE_VOLUME", GLUSTER_CLI_CREATE_VOLUME,    glusterd_handle_create_volume,         NULL, 0, DRC_NA},
        [GLUSTER_CLI_DEFRAG_VOLUME]      = { "CLI_DEFRAG_VOLUME", GLUSTER_CLI_DEFRAG_VOLUME,    glusterd_handle_defrag_volume,         NULL, 0, DRC_NA},
        [GLUSTER_CLI_DEPROBE]            = { "FRIEND_REMOVE",     GLUSTER_CLI_DEPROBE,          glusterd_handle_cli_deprobe,           NULL, 0, DRC_NA},
        [GLUSTER_CLI_LIST_FRIENDS]       = { "LIST_FRIENDS",      GLUSTER_CLI_LIST_FRIENDS,     glusterd_handle_cli_list_friends,      NULL, 0, DRC_NA},
        [GLUSTER_CLI_UUID_RESET]         = { "UUID_RESET",        GLUSTER_CLI_UUID_RESET,       glusterd_handle_cli_uuid_reset,        NULL, 0, DRC_NA},
        [GLUSTER_CLI_UUID_GET]           = { "UUID_GET",          GLUSTER_CLI_UUID_GET,         glusterd_handle_cli_uuid_get,          NULL, 0, DRC_NA},
        [GLUSTER_CLI_START_VOLUME]       = { "START_VOLUME",      GLUSTER_CLI_START_VOLUME,     glusterd_handle_cli_start_volume,      NULL, 0, DRC_NA},
        [GLUSTER_CLI_STOP_VOLUME]        = { "STOP_VOLUME",       GLUSTER_CLI_STOP_VOLUME,      glusterd_handle_cli_stop_volume,       NULL, 0, DRC_NA},
        [GLUSTER_CLI_DELETE_VOLUME]      = { "DELETE_VOLUME",     GLUSTER_CLI_DELETE_VOLUME,    glusterd_handle_cli_delete_volume,     NULL, 0, DRC_NA},
        [GLUSTER_CLI_GET_VOLUME]         = { "GET_VOLUME",        GLUSTER_CLI_GET_VOLUME,       glusterd_handle_cli_get_volume,        NULL, 0, DRC_NA},
        [GLUSTER_CLI_ADD_BRICK]          = { "ADD_BRICK",         GLUSTER_CLI_ADD_BRICK,        glusterd_handle_add_brick,             NULL, 0, DRC_NA},
        [GLUSTER_CLI_ATTACH_TIER]        = { "ATTACH_TIER",       GLUSTER_CLI_ATTACH_TIER,      glusterd_handle_attach_tier,           NULL, 0, DRC_NA},
        [GLUSTER_CLI_REPLACE_BRICK]      = { "REPLACE_BRICK",     GLUSTER_CLI_REPLACE_BRICK,    glusterd_handle_replace_brick,         NULL, 0, DRC_NA},
        [GLUSTER_CLI_REMOVE_BRICK]       = { "REMOVE_BRICK",      GLUSTER_CLI_REMOVE_BRICK,     glusterd_handle_remove_brick,          NULL, 0, DRC_NA},
        [GLUSTER_CLI_LOG_ROTATE]         = { "LOG FILENAME",      GLUSTER_CLI_LOG_ROTATE,       glusterd_handle_log_rotate,            NULL, 0, DRC_NA},
        [GLUSTER_CLI_SET_VOLUME]         = { "SET_VOLUME",        GLUSTER_CLI_SET_VOLUME,       glusterd_handle_set_volume,            NULL, 0, DRC_NA},
        [GLUSTER_CLI_SYNC_VOLUME]        = { "SYNC_VOLUME",       GLUSTER_CLI_SYNC_VOLUME,      glusterd_handle_sync_volume,           NULL, 0, DRC_NA},
        [GLUSTER_CLI_RESET_VOLUME]       = { "RESET_VOLUME",      GLUSTER_CLI_RESET_VOLUME,     glusterd_handle_reset_volume,          NULL, 0, DRC_NA},
        [GLUSTER_CLI_FSM_LOG]            = { "FSM_LOG",           GLUSTER_CLI_FSM_LOG,          glusterd_handle_fsm_log,               NULL, 0, DRC_NA},
        [GLUSTER_CLI_GSYNC_SET]          = { "GSYNC_SET",         GLUSTER_CLI_GSYNC_SET,        glusterd_handle_gsync_set,             NULL, 0, DRC_NA},
        [GLUSTER_CLI_PROFILE_VOLUME]     = { "STATS_VOLUME",      GLUSTER_CLI_PROFILE_VOLUME,   glusterd_handle_cli_profile_volume,    NULL, 0, DRC_NA},
        [GLUSTER_CLI_QUOTA]              = { "QUOTA",             GLUSTER_CLI_QUOTA,            glusterd_handle_quota,                 NULL, 0, DRC_NA},
        [GLUSTER_CLI_GETWD]              = { "GETWD",             GLUSTER_CLI_GETWD,            glusterd_handle_getwd,                 NULL, 1, DRC_NA},
        [GLUSTER_CLI_STATUS_VOLUME]      = {"STATUS_VOLUME",      GLUSTER_CLI_STATUS_VOLUME,    glusterd_handle_status_volume,         NULL, 0, DRC_NA},
        [GLUSTER_CLI_MOUNT]              = { "MOUNT",             GLUSTER_CLI_MOUNT,            glusterd_handle_mount,                 NULL, 1, DRC_NA},
        [GLUSTER_CLI_UMOUNT]             = { "UMOUNT",            GLUSTER_CLI_UMOUNT,           glusterd_handle_umount,                NULL, 1, DRC_NA},
        [GLUSTER_CLI_HEAL_VOLUME]        = { "HEAL_VOLUME",       GLUSTER_CLI_HEAL_VOLUME,      glusterd_handle_cli_heal_volume,       NULL, 0, DRC_NA},
        [GLUSTER_CLI_STATEDUMP_VOLUME]   = {"STATEDUMP_VOLUME",   GLUSTER_CLI_STATEDUMP_VOLUME, glusterd_handle_cli_statedump_volume,  NULL, 0, DRC_NA},
        [GLUSTER_CLI_LIST_VOLUME]        = {"LIST_VOLUME",        GLUSTER_CLI_LIST_VOLUME,      glusterd_handle_cli_list_volume,       NULL, 0, DRC_NA},
        [GLUSTER_CLI_CLRLOCKS_VOLUME]    = {"CLEARLOCKS_VOLUME",  GLUSTER_CLI_CLRLOCKS_VOLUME,  glusterd_handle_cli_clearlocks_volume, NULL, 0, DRC_NA},
        [GLUSTER_CLI_COPY_FILE]          = {"COPY_FILE",          GLUSTER_CLI_COPY_FILE,        glusterd_handle_copy_file,             NULL, 0, DRC_NA},
        [GLUSTER_CLI_SYS_EXEC]           = {"SYS_EXEC",           GLUSTER_CLI_SYS_EXEC,         glusterd_handle_sys_exec,              NULL, 0, DRC_NA},
        [GLUSTER_CLI_SNAP]               = {"SNAP",               GLUSTER_CLI_SNAP,             glusterd_handle_snapshot,              NULL, 0, DRC_NA},
        [GLUSTER_CLI_BARRIER_VOLUME]     = {"BARRIER_VOLUME",     GLUSTER_CLI_BARRIER_VOLUME,   glusterd_handle_barrier,               NULL, 0, DRC_NA},
        [GLUSTER_CLI_GANESHA]            = { "GANESHA"  ,         GLUSTER_CLI_GANESHA,          glusterd_handle_ganesha_cmd,           NULL, 0, DRC_NA},
        [GLUSTER_CLI_GET_VOL_OPT]        = {"GET_VOL_OPT",        GLUSTER_CLI_GET_VOL_OPT,      glusterd_handle_get_vol_opt,           NULL, 0, DRC_NA},
        [GLUSTER_CLI_BITROT]             = {"BITROT",             GLUSTER_CLI_BITROT,           glusterd_handle_bitrot,                NULL, 0, DRC_NA},
        [GLUSTER_CLI_GET_STATE]          = {"GET_STATE",          GLUSTER_CLI_GET_STATE,        glusterd_handle_get_state,             NULL, 0, DRC_NA},
        [GLUSTER_CLI_RESET_BRICK]        = {"RESET_BRICK",        GLUSTER_CLI_RESET_BRICK,      glusterd_handle_reset_brick,           NULL, 0, DRC_NA},
        [GLUSTER_CLI_TIER]               = {"TIER",               GLUSTER_CLI_TIER,             glusterd_handle_tier,                  NULL, 0, DRC_NA},
        [GLUSTER_CLI_REMOVE_TIER_BRICK]  = {"REMOVE_TIER_BRICK",  GLUSTER_CLI_REMOVE_TIER_BRICK,             glusterd_handle_tier,                  NULL, 0, DRC_NA},
};

struct rpcsvc_program gd_svc_cli_prog = {
        .progname  = "GlusterD svc cli",
        .prognum   = GLUSTER_CLI_PROGRAM,
        .progver   = GLUSTER_CLI_VERSION,
        .numactors = GLUSTER_CLI_MAXVALUE,
        .actors    = gd_svc_cli_actors,
	.synctask  = _gf_true,
};

/**
 * This set of RPC progs are deemed to be trusted. Most of the actors support
 * read only queries, the only exception being MOUNT/UMOUNT which is required
 * by geo-replication to supprt unprivileged master -> slave sessions.
 */
rpcsvc_actor_t gd_svc_cli_trusted_actors[GLUSTER_CLI_MAXVALUE] = {
        [GLUSTER_CLI_LIST_FRIENDS]       = { "LIST_FRIENDS",      GLUSTER_CLI_LIST_FRIENDS,     glusterd_handle_cli_list_friends,      NULL, 0, DRC_NA},
        [GLUSTER_CLI_UUID_GET]           = { "UUID_GET",          GLUSTER_CLI_UUID_GET,         glusterd_handle_cli_uuid_get,          NULL, 0, DRC_NA},
        [GLUSTER_CLI_GET_VOLUME]         = { "GET_VOLUME",        GLUSTER_CLI_GET_VOLUME,       glusterd_handle_cli_get_volume,        NULL, 0, DRC_NA},
        [GLUSTER_CLI_GETWD]              = { "GETWD",             GLUSTER_CLI_GETWD,            glusterd_handle_getwd,                 NULL, 1, DRC_NA},
        [GLUSTER_CLI_STATUS_VOLUME]      = {"STATUS_VOLUME",      GLUSTER_CLI_STATUS_VOLUME,    glusterd_handle_status_volume,         NULL, 0, DRC_NA},
        [GLUSTER_CLI_LIST_VOLUME]        = {"LIST_VOLUME",        GLUSTER_CLI_LIST_VOLUME,      glusterd_handle_cli_list_volume,       NULL, 0, DRC_NA},
        [GLUSTER_CLI_MOUNT]              = { "MOUNT",             GLUSTER_CLI_MOUNT,            glusterd_handle_mount,                 NULL, 1, DRC_NA},
        [GLUSTER_CLI_UMOUNT]             = { "UMOUNT",            GLUSTER_CLI_UMOUNT,           glusterd_handle_umount,                NULL, 1, DRC_NA},
};

struct rpcsvc_program gd_svc_cli_trusted_progs = {
        .progname  = "GlusterD svc cli read-only",
        .prognum   = GLUSTER_CLI_PROGRAM,
        .progver   = GLUSTER_CLI_VERSION,
        .numactors = GLUSTER_CLI_MAXVALUE,
        .actors    = gd_svc_cli_trusted_actors,
	.synctask  = _gf_true,
};
