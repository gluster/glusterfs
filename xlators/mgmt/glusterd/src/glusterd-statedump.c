/*
   Copyright (c) 2014 Red Hat, Inc. <http://www.redhat.com>
   This file is part of GlusterFS.

   This file is licensed to you under your choice of the GNU Lesser
   General Public License, version 3 or any later version (LGPLv3 or
   later), or the GNU General Public License, version 2 (GPLv2), in all
   cases as published by the Free Software Foundation.
*/

#include <glusterfs/statedump.h>
#include "glusterd.h"
#include "glusterd-shd-svc.h"
#include "glusterd-quotad-svc.h"
#include "glusterd-locks.h"
#include "glusterd-messages.h"

static void
glusterd_dump_peer(glusterd_peerinfo_t *peerinfo, char *input_key, int index,
                   gf_boolean_t xpeers)
{
    char subkey[GF_DUMP_MAX_BUF_LEN + 11] = "";
    char key[GF_DUMP_MAX_BUF_LEN] = "";

    strncpy(key, input_key, sizeof(key) - 1);

    snprintf(subkey, sizeof(subkey), "%s%d", key, index);

    gf_proc_dump_build_key(key, subkey, "uuid");
    gf_proc_dump_write(key, "%s", uuid_utoa(peerinfo->uuid));

    gf_proc_dump_build_key(key, subkey, "hostname");
    gf_proc_dump_write(key, "%s", peerinfo->hostname);

    gf_proc_dump_build_key(key, subkey, "port");
    gf_proc_dump_write(key, "%d", peerinfo->port);

    gf_proc_dump_build_key(key, subkey, "state");
    gf_proc_dump_write(key, "%d", peerinfo->state.state);

    gf_proc_dump_build_key(key, subkey, "quorum-action");
    gf_proc_dump_write(key, "%d", peerinfo->quorum_action);

    gf_proc_dump_build_key(key, subkey, "quorum-contrib");
    gf_proc_dump_write(key, "%d", peerinfo->quorum_contrib);

    gf_proc_dump_build_key(key, subkey, "detaching");
    gf_proc_dump_write(key, "%d", peerinfo->detaching);

    gf_proc_dump_build_key(key, subkey, "locked");
    gf_proc_dump_write(key, "%d", peerinfo->locked);
}

static void
glusterd_dump_peer_rpcstat(glusterd_peerinfo_t *peerinfo, char *input_key,
                           int index)
{
    rpc_clnt_connection_t *conn = NULL;
    int ret = -1;
    rpc_clnt_t *rpc = NULL;
    char rpcsvc_peername[RPCSVC_PEER_STRLEN] = "";
    char subkey[GF_DUMP_MAX_BUF_LEN + 11] = "";
    char key[GF_DUMP_MAX_BUF_LEN] = "";

    strncpy(key, input_key, sizeof(key) - 1);

    /* Dump the rpc connection statistics */
    rpc = peerinfo->rpc;
    if (rpc) {
        conn = &rpc->conn;
        snprintf(subkey, sizeof(subkey), "%s%d", key, index);
        ret = rpcsvc_transport_peername(conn->trans, (char *)&rpcsvc_peername,
                                        sizeof(rpcsvc_peername));
        if (!ret) {
            gf_proc_dump_build_key(key, subkey, "rpc.peername");
            gf_proc_dump_write(key, "%s", rpcsvc_peername);
        }
        gf_proc_dump_build_key(key, subkey, "rpc.connected");
        gf_proc_dump_write(key, "%d", conn->connected);

        gf_proc_dump_build_key(key, subkey, "rpc.total-bytes-read");
        gf_proc_dump_write(key, "%" PRIu64, conn->trans->total_bytes_read);

        gf_proc_dump_build_key(key, subkey, "rpc.total-bytes-written");
        gf_proc_dump_write(key, "%" PRIu64, conn->trans->total_bytes_write);

        gf_proc_dump_build_key(key, subkey, "rpc.ping_msgs_sent");
        gf_proc_dump_write(key, "%" PRIu64, conn->pingcnt);

        gf_proc_dump_build_key(key, subkey, "rpc.msgs_sent");
        gf_proc_dump_write(key, "%" PRIu64, conn->msgcnt);
    }
}

static void
glusterd_dump_client_details(glusterd_conf_t *conf)
{
    rpc_transport_t *xprt = NULL;
    char key[GF_DUMP_MAX_BUF_LEN] = "";
    char subkey[50] = "";
    int index = 1;

    pthread_mutex_lock(&conf->xprt_lock);
    {
        list_for_each_entry(xprt, &conf->xprt_list, list)
        {
            snprintf(subkey, sizeof(subkey), "glusterd.client%d", index);

            gf_proc_dump_build_key(key, subkey, "identifier");
            gf_proc_dump_write(key, "%s", xprt->peerinfo.identifier);

            gf_proc_dump_build_key(key, subkey, "volname");
            gf_proc_dump_write(key, "%s", xprt->peerinfo.volname);

            gf_proc_dump_build_key(key, subkey, "max-op-version");
            gf_proc_dump_write(key, "%u", xprt->peerinfo.max_op_version);

            gf_proc_dump_build_key(key, subkey, "min-op-version");
            gf_proc_dump_write(key, "%u", xprt->peerinfo.min_op_version);
            index++;
        }
    }
    pthread_mutex_unlock(&conf->xprt_lock);
}

/* The following function is just for dumping mgmt_v3_lock dictionary, any other
 * dict passed to this API will not work */

static void
glusterd_dict_mgmt_v3_lock_statedump(dict_t *dict)
{
    int ret = 0;
    int dumplen = 0;
    data_pair_t *trav = NULL;
    char key[GF_DUMP_MAX_BUF_LEN] = "";
    char dump[64 * 1024] = "";

    if (!dict) {
        gf_msg_callingfn("glusterd", GF_LOG_WARNING, EINVAL, GD_MSG_DICT_EMPTY,
                         "dict NULL");
        goto out;
    }
    for (trav = dict->members_list; trav; trav = trav->next) {
        if (strstr(trav->key, "debug.last-success-bt") != NULL) {
            ret = snprintf(&dump[dumplen], sizeof(dump) - dumplen, "\n\t%s:%s",
                           trav->key, trav->value->data);
        } else {
            ret = snprintf(
                &dump[dumplen], sizeof(dump) - dumplen, "\n\t%s:%s", trav->key,
                uuid_utoa(((glusterd_mgmt_v3_lock_obj *)(trav->value->data))
                              ->lock_owner));
        }
        if ((ret == -1) || !ret)
            return;
        dumplen += ret;
    }

    if (dumplen) {
        gf_proc_dump_build_key(key, "glusterd", "mgmt_v3_lock");
        gf_proc_dump_write(key, "%s", dump);
    }

out:
    return;
}

int
glusterd_dump_priv(xlator_t *this)
{
    glusterd_conf_t *priv = NULL;
    struct pmap_registry *pmap = NULL;
    struct pmap_ports *tmp_port = NULL;
    char key[GF_DUMP_MAX_BUF_LEN] = "";

    priv = this->private;
    if (!priv)
        return 0;

    pmap = priv->pmap;

    gf_proc_dump_build_key(key, "xlator.glusterd", "priv");
    gf_proc_dump_add_section("%s", key);

    pthread_mutex_lock(&priv->mutex);
    {
        gf_proc_dump_build_key(key, "glusterd", "my-uuid");
        gf_proc_dump_write(key, "%s", uuid_utoa(priv->uuid));

        gf_proc_dump_build_key(key, "glusterd", "working-directory");
        gf_proc_dump_write(key, "%s", priv->workdir);

        gf_proc_dump_build_key(key, "glusterd", "max-op-version");
        gf_proc_dump_write(key, "%d", GD_OP_VERSION_MAX);

        gf_proc_dump_build_key(key, "glusterd", "min-op-version");
        gf_proc_dump_write(key, "%d", GD_OP_VERSION_MIN);

        gf_proc_dump_build_key(key, "glusterd", "current-op-version");
        gf_proc_dump_write(key, "%d", priv->op_version);

        gf_proc_dump_build_key(key, "glusterd", "ping-timeout");
        gf_proc_dump_write(key, "%d", priv->ping_timeout);

        gf_proc_dump_build_key(key, "glusterd", "shd.online");
        gf_proc_dump_write(key, "%d", priv->shd_svc.online);

#ifdef BUILD_GNFS
        gf_proc_dump_build_key(key, "glusterd", "nfs.online");
        gf_proc_dump_write(key, "%d", priv->nfs_svc.online);
#endif
        gf_proc_dump_build_key(key, "glusterd", "quotad.online");
        gf_proc_dump_write(key, "%d", priv->quotad_svc.online);

        gf_proc_dump_build_key(key, "glusterd", "bitd.online");
        gf_proc_dump_write(key, "%d", priv->bitd_svc.online);

        gf_proc_dump_build_key(key, "glusterd", "scrub.online");
        gf_proc_dump_write(key, "%d", priv->scrub_svc.online);

        /* Dump peer details */
        GLUSTERD_DUMP_PEERS(&priv->peers, uuid_list, _gf_false);

        if (pmap) {
            /* Dump brick name and port associated with it */
            cds_list_for_each_entry(tmp_port, &pmap->ports, port_list)
            {
                gf_proc_dump_build_key(key, "glusterd", "brick_port");
                gf_proc_dump_write(key, "%d", tmp_port->port);
                gf_proc_dump_build_key(key, "glusterd", "brickname");
                gf_proc_dump_write(key, "%s", tmp_port->brickname);
            }
        }

        /* Dump client details */
        glusterd_dump_client_details(priv);

        /* Dump mgmt_v3_lock from the dictionary if any */
        glusterd_dict_mgmt_v3_lock_statedump(priv->mgmt_v3_lock);
        dict_dump_to_statedump(priv->opts, "options", "glusterd");
    }
    pthread_mutex_unlock(&priv->mutex);

    return 0;
}
