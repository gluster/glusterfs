/*
   Copyright (c) 2014 Red Hat, Inc. <http://www.redhat.com>
   This file is part of GlusterFS.

   This file is licensed to you under your choice of the GNU Lesser
   General Public License, version 3 or any later version (LGPLv3 or
   later), or the GNU General Public License, version 2 (GPLv2), in all
   cases as published by the Free Software Foundation.
*/

#include "glusterd-peer-utils.h"
#include "glusterd-store.h"
int32_t
glusterd_friend_cleanup (glusterd_peerinfo_t *peerinfo)
{
        GF_ASSERT (peerinfo);
        glusterd_peerctx_t      *peerctx = NULL;
        gf_boolean_t            quorum_action = _gf_false;
        glusterd_conf_t         *priv = THIS->private;

        if (peerinfo->quorum_contrib != QUORUM_NONE)
                quorum_action = _gf_true;
        if (peerinfo->rpc) {
                /* cleanup the saved-frames before last unref */
                synclock_unlock (&priv->big_lock);
                rpc_clnt_connection_cleanup (&peerinfo->rpc->conn);
                synclock_lock (&priv->big_lock);

                peerctx = peerinfo->rpc->mydata;
                peerinfo->rpc->mydata = NULL;
                peerinfo->rpc = glusterd_rpc_clnt_unref (priv, peerinfo->rpc);
                peerinfo->rpc = NULL;
                if (peerctx) {
                        GF_FREE (peerctx->errstr);
                        GF_FREE (peerctx);
                }
        }
        glusterd_peer_destroy (peerinfo);

        if (quorum_action)
                glusterd_do_quorum_action ();
        return 0;
}

int32_t
glusterd_peer_destroy (glusterd_peerinfo_t *peerinfo)
{
        int32_t                         ret = -1;
        glusterd_peer_hostname_t *hostname = NULL;

        if (!peerinfo)
                goto out;

        ret = glusterd_store_delete_peerinfo (peerinfo);

        if (ret) {
                gf_log ("", GF_LOG_ERROR, "Deleting peer info failed");
        }

        list_del_init (&peerinfo->uuid_list);

        list_for_each_entry (hostname, &peerinfo->hostnames, hostname_list) {
                glusterd_peer_hostname_free (hostname);
        }

        glusterd_sm_tr_log_delete (&peerinfo->sm_log);
        GF_FREE (peerinfo);
        peerinfo = NULL;

        ret = 0;

out:
        return ret;
}

glusterd_peerinfo_t *
glusterd_search_hostname (xlator_t *this, const char *hoststr1,
                          const char *hoststr2)
{
        glusterd_peerinfo_t            *peer            = NULL;
        glusterd_peer_hostname_t       *tmphost         = NULL;
        glusterd_conf_t                *priv            = this->private;

        GF_ASSERT (priv);
        GF_ASSERT (hoststr1);

        list_for_each_entry (peer, &priv->peers, uuid_list)
                list_for_each_entry (tmphost, &peer->hostnames,hostname_list)
                        if (!strncasecmp (tmphost->hostname, hoststr1, 1024) ||
                            (hoststr2 &&
                             !strncasecmp (tmphost->hostname,hoststr2, 1024))){

                                gf_log (this->name, GF_LOG_DEBUG,
                                        "Friend %s found.. state: %d",
                                        tmphost->hostname, peer->state.state);
                                return peer;
                        }

        return NULL;
}

int
glusterd_friend_find_by_hostname (const char *hoststr,
                                  glusterd_peerinfo_t  **peerinfo)
{
        int                             ret             = -1;
        struct addrinfo                *addr            = NULL;
        struct addrinfo                *p               = NULL;
        char                           *host            = NULL;
        struct sockaddr_in6            *s6              = NULL;
        struct sockaddr_in             *s4              = NULL;
        struct in_addr                 *in_addr         = NULL;
        char                            hname[1024]     = {0,};
        xlator_t                       *this            = NULL;


        this = THIS;
        GF_ASSERT (hoststr);
        GF_ASSERT (peerinfo);

        *peerinfo = NULL;

        *peerinfo = glusterd_search_hostname (this, hoststr, NULL);
        if (*peerinfo)
                return 0;

        ret = getaddrinfo (hoststr, NULL, NULL, &addr);
        if (ret != 0) {
                gf_log (this->name, GF_LOG_ERROR,
                        "error in getaddrinfo: %s\n",
                        gai_strerror(ret));
                goto out;
        }

        for (p = addr; p != NULL; p = p->ai_next) {
                switch (p->ai_family) {
                        case AF_INET:
                                s4 = (struct sockaddr_in *) p->ai_addr;
                                in_addr = &s4->sin_addr;
                                break;
                        case AF_INET6:
                                s6 = (struct sockaddr_in6 *) p->ai_addr;
                                in_addr =(struct in_addr *) &s6->sin6_addr;
                                break;
                       default: ret = -1;
                                goto out;
                }
                host = inet_ntoa(*in_addr);

                ret = getnameinfo (p->ai_addr, p->ai_addrlen, hname,
                                   1024, NULL, 0, 0);
                if (ret)
                        goto out;

                *peerinfo = glusterd_search_hostname (this, host, hname);
                if (*peerinfo) {
                        freeaddrinfo (addr);
                        return 0;
                }
        }

out:
        gf_log (this->name, GF_LOG_DEBUG, "Unable to find friend: %s", hoststr);
        if (addr)
                freeaddrinfo (addr);
        return -1;
}

int
glusterd_hostname_to_uuid (char *hostname, uuid_t uuid)
{
        GF_ASSERT (hostname);
        GF_ASSERT (uuid);

        glusterd_peerinfo_t     *peerinfo = NULL;
        glusterd_conf_t         *priv = NULL;
        int                     ret = -1;
        xlator_t                *this = NULL;

        this = THIS;
        GF_ASSERT (this);
        priv = this->private;
        GF_ASSERT (priv);

        ret = glusterd_friend_find_by_hostname (hostname, &peerinfo);
        if (ret) {
                if (gf_is_local_addr (hostname)) {
                        uuid_copy (uuid, MY_UUID);
                        ret = 0;
                } else {
                        goto out;
                }
        } else {
                uuid_copy (uuid, peerinfo->uuid);
        }

out:
        gf_log (this->name, GF_LOG_DEBUG, "returning %d", ret);
        return ret;
}

int
glusterd_friend_find_by_uuid (uuid_t uuid,
                              glusterd_peerinfo_t  **peerinfo)
{
        int                     ret = -1;
        glusterd_conf_t         *priv = NULL;
        glusterd_peerinfo_t     *entry = NULL;
        xlator_t                *this = NULL;

        this = THIS;
        GF_ASSERT (this);
        GF_ASSERT (peerinfo);

        *peerinfo = NULL;
        priv    = this->private;

        GF_ASSERT (priv);

        if (uuid_is_null (uuid))
                return -1;

        list_for_each_entry (entry, &priv->peers, uuid_list) {
                if (!uuid_compare (entry->uuid, uuid)) {

                        gf_log (this->name, GF_LOG_DEBUG,
                                 "Friend found... state: %s",
                        glusterd_friend_sm_state_name_get (entry->state.state));
                        *peerinfo = entry;
                        return 0;
                }
        }

        gf_log (this->name, GF_LOG_DEBUG, "Friend with uuid: %s, not found",
                uuid_utoa (uuid));
        return ret;
}

/* glusterd_peerinfo_new will create a new peerinfo object and sets its address
 * into @peerinfo. If any other parameters are given, they will be set in the
 * peerinfo object.
 */
int
glusterd_peerinfo_new (glusterd_peerinfo_t **peerinfo,
                       glusterd_friend_sm_state_t state, uuid_t *uuid,
                       const char *hostname, int port)
{
        glusterd_peerinfo_t      *new_peer = NULL;
        int                      ret = -1;

        GF_ASSERT (peerinfo);
        if (!peerinfo)
                goto out;

        new_peer = GF_CALLOC (1, sizeof (*new_peer), gf_gld_mt_peerinfo_t);
        if (!new_peer)
                goto out;

        INIT_LIST_HEAD (&new_peer->uuid_list);

        new_peer->state.state = state;

        INIT_LIST_HEAD (&new_peer->hostnames);
        if (hostname) {
                ret = gd_add_address_to_peer (new_peer, hostname);
                if (ret)
                        goto out;
        }

        if (uuid) {
                uuid_copy (new_peer->uuid, *uuid);
        }

        ret = glusterd_sm_tr_log_init (&new_peer->sm_log,
                                       glusterd_friend_sm_state_name_get,
                                       glusterd_friend_sm_event_name_get,
                                       GLUSTERD_TR_LOG_SIZE);
        if (ret)
                goto out;

        if (new_peer->state.state == GD_FRIEND_STATE_BEFRIENDED)
                new_peer->quorum_contrib = QUORUM_WAITING;
        new_peer->port = port;
        *peerinfo = new_peer;
out:
        if (ret && new_peer)
                glusterd_friend_cleanup (new_peer);
        gf_log ("", GF_LOG_DEBUG, "returning %d", ret);
        return ret;
}

gf_boolean_t
glusterd_peerinfo_is_uuid_unknown (glusterd_peerinfo_t *peerinfo)
{
        GF_ASSERT (peerinfo);

        if (uuid_is_null (peerinfo->uuid))
                return _gf_true;
        return _gf_false;
}

/* Check if the all peers are connected and befriended, except the peer
 * specified (the peer being detached)
 */
gf_boolean_t
glusterd_chk_peers_connected_befriended (uuid_t skip_uuid)
{
        gf_boolean_t            ret = _gf_true;
        glusterd_peerinfo_t     *peerinfo = NULL;
        glusterd_conf_t         *priv = NULL;

        priv= THIS->private;
        GF_ASSERT (priv);

        list_for_each_entry (peerinfo, &priv->peers, uuid_list) {

                if (!uuid_is_null (skip_uuid) && !uuid_compare (skip_uuid,
                                                           peerinfo->uuid))
                        continue;

                if ((GD_FRIEND_STATE_BEFRIENDED != peerinfo->state.state)
                    || !(peerinfo->connected)) {
                        ret = _gf_false;
                        break;
                }
        }
        gf_log (THIS->name, GF_LOG_DEBUG, "Returning %s",
                (ret?"TRUE":"FALSE"));
        return ret;
}

/* Return hostname for given uuid if it exists
 * else return NULL
 */
char *
glusterd_uuid_to_hostname (uuid_t uuid)
{
        char                    *hostname = NULL;
        glusterd_conf_t         *priv = NULL;
        glusterd_peerinfo_t     *entry = NULL;

        priv = THIS->private;
        GF_ASSERT (priv);

        if (!uuid_compare (MY_UUID, uuid)) {
                hostname = gf_strdup ("localhost");
        }
        if (!list_empty (&priv->peers)) {
                list_for_each_entry (entry, &priv->peers, uuid_list) {
                        if (!uuid_compare (entry->uuid, uuid)) {
                                hostname = gf_strdup (entry->hostname);
                                break;
                        }
                }
        }

        return hostname;
}

char*
gd_peer_uuid_str (glusterd_peerinfo_t *peerinfo)
{
        if ((peerinfo == NULL) || uuid_is_null (peerinfo->uuid))
                return NULL;

        if (peerinfo->uuid_str[0] == '\0')
                uuid_utoa_r (peerinfo->uuid, peerinfo->uuid_str);

        return peerinfo->uuid_str;
}

gf_boolean_t
glusterd_are_vol_all_peers_up (glusterd_volinfo_t *volinfo,
                               struct list_head *peers,
                               char **down_peerstr)
{
        glusterd_peerinfo_t   *peerinfo  = NULL;
        glusterd_brickinfo_t  *brickinfo = NULL;
        gf_boolean_t           ret       = _gf_false;

        list_for_each_entry (brickinfo, &volinfo->bricks, brick_list) {
                if (!uuid_compare (brickinfo->uuid, MY_UUID))
                        continue;

                list_for_each_entry (peerinfo, peers, uuid_list) {
                        if (uuid_compare (peerinfo->uuid, brickinfo->uuid))
                                continue;

                        /*Found peer who owns the brick, return false
                         * if peer is not connected or not friend */
                        if (!(peerinfo->connected) ||
                           (peerinfo->state.state !=
                             GD_FRIEND_STATE_BEFRIENDED)) {
                                *down_peerstr = gf_strdup (peerinfo->hostname);
                                gf_log ("", GF_LOG_DEBUG, "Peer %s is down. ",
                                        peerinfo->hostname);
                                goto out;
                        }
                }
       }

        ret = _gf_true;
out:
        gf_log ("", GF_LOG_DEBUG, "Returning %d", ret);
        return ret;
}

int32_t
glusterd_peer_hostname_new (const char *hostname,
                            glusterd_peer_hostname_t **name)
{
        glusterd_peer_hostname_t        *peer_hostname = NULL;
        int32_t                         ret = -1;

        GF_ASSERT (hostname);
        GF_ASSERT (name);

        peer_hostname = GF_CALLOC (1, sizeof (*peer_hostname),
                                   gf_gld_mt_peer_hostname_t);

        if (!peer_hostname)
                goto out;

        peer_hostname->hostname = gf_strdup (hostname);
        INIT_LIST_HEAD (&peer_hostname->hostname_list);

        *name = peer_hostname;
        ret = 0;

out:
        gf_log ("", GF_LOG_DEBUG, "Returning %d", ret);
        return ret;
}

void
glusterd_peer_hostname_free (glusterd_peer_hostname_t *name)
{
        if (!name)
                return;

        list_del_init (&name->hostname_list);

        GF_FREE (name->hostname);
        name->hostname = NULL;

        GF_FREE (name);

        return;
}

gf_boolean_t
gd_peer_has_address (glusterd_peerinfo_t *peerinfo, const char *address)
{
        gf_boolean_t ret = _gf_false;
        glusterd_peer_hostname_t *hostname = NULL;

        GF_VALIDATE_OR_GOTO ("glusterd", (peerinfo != NULL), out);
        GF_VALIDATE_OR_GOTO ("glusterd", (address != NULL), out);

        list_for_each_entry (hostname, &peerinfo->hostnames, hostname_list) {
                if (strcmp (hostname->hostname, address) == 0) {
                        ret = _gf_true;
                        break;
                }
        }

out:
        return ret;
}

int
gd_add_address_to_peer (glusterd_peerinfo_t *peerinfo, const char *address)
{

        int ret = -1;
        glusterd_peer_hostname_t *hostname = NULL;

        GF_VALIDATE_OR_GOTO ("glusterd", (peerinfo != NULL), out);
        GF_VALIDATE_OR_GOTO ("glusterd", (address != NULL), out);

        if (gd_peer_has_address (peerinfo, address)) {
                ret = 0;
                goto out;
        }

        ret = glusterd_peer_hostname_new (address, &hostname);
        if (ret)
                goto out;

        list_add_tail (&hostname->hostname_list, &peerinfo->hostnames);

        ret = 0;
out:
        return ret;
}

/* gd_add_friend_to_dict() adds details of @friend into @dict with the given
 * @prefix. All the parameters are compulsary.
 *
 * The complete address list is added to the dict only if the cluster op-version
 * is >= GD_OP_VERSION_3_6_0
 */
int
gd_add_friend_to_dict (glusterd_peerinfo_t *friend, dict_t *dict,
                       const char *prefix)
{
        int                       ret      = -1;
        xlator_t                 *this     = NULL;
        glusterd_conf_t          *conf     = NULL;
        char                      key[100] = {0,};
        glusterd_peer_hostname_t *address  = NULL;
        int                       count    = 0;

        this = THIS;
        GF_VALIDATE_OR_GOTO ("glusterd", (this != NULL), out);

        conf = this->private;
        GF_VALIDATE_OR_GOTO (this->name, (conf != NULL), out);

        GF_VALIDATE_OR_GOTO (this->name, (friend != NULL), out);
        GF_VALIDATE_OR_GOTO (this->name, (dict != NULL), out);
        GF_VALIDATE_OR_GOTO (this->name, (prefix != NULL), out);

        snprintf (key, sizeof (key), "%s.uuid", prefix);
        ret = dict_set_dynstr_with_alloc (dict, key, uuid_utoa (friend->uuid));
        if (ret) {
                gf_log (this->name, GF_LOG_ERROR,
                        "Failed to set key %s in dict", key);
                goto out;
        }

        /* Setting the first hostname from the list with this key for backward
         * compatability
         */
        memset (key, 0, sizeof (key));
        snprintf (key, sizeof (key), "%s.hostname", prefix);
        address = list_entry (&friend->hostnames, glusterd_peer_hostname_t,
                              hostname_list);
        if (!address) {
                ret = -1;
                gf_log (this->name, GF_LOG_ERROR, "Could not retrieve first "
                        "address for peer");
                goto out;
        }
        ret = dict_set_dynstr_with_alloc (dict, key, address->hostname);
        if (ret) {
                gf_log (this->name, GF_LOG_ERROR,
                        "Failed to set key %s in dict", key);
                goto out;
        }

        if (conf->op_version < GD_OP_VERSION_3_6_0) {
                ret = 0;
                goto out;
        }

        address = NULL;
        list_for_each_entry (address, &friend->hostnames, hostname_list) {
                GF_VALIDATE_OR_GOTO (this->name, (address != NULL), out);

                memset (key, 0, sizeof (key));
                snprintf (key, sizeof (key), "%s.hostname%d", prefix, count);
                ret = dict_set_dynstr_with_alloc (dict, key, address->hostname);
                if (ret) {
                        gf_log (this->name, GF_LOG_ERROR,
                                        "Failed to set key %s in dict", key);
                        goto out;
                }
                count++;
                address = NULL;
        }
        memset (key, 0, sizeof (key));
        snprintf (key, sizeof (key), "%s.address-count", prefix);
        ret = dict_set_int32 (dict, key, count);
        if (ret)
                gf_log (this->name, GF_LOG_ERROR,
                        "Failed to set key %s in dict", key);

out:
        gf_log (this ? this->name : "glusterd", GF_LOG_DEBUG, "Returning %d",
                ret);
        return ret;
}

/* gd_peerinfo_from_dict creates a peerinfo object from details of peer with
 * @prefix in @dict and stores its address into @peerinfo.
 * All the parmeters are necessary
 */
int
gd_peerinfo_from_dict (dict_t *dict, char *prefix,
                       glusterd_peerinfo_t **peerinfo)
{
        int                  ret      = -1;
        xlator_t            *this     = NULL;
        glusterd_conf_t     *conf     = NULL;
        glusterd_peerinfo_t *new_peer = NULL;
        char                 key[100] = {0,};
        char                *uuid_str = NULL;
        char                *hostname = NULL;
        int                  count    = 0;
        int                  i        = 0;

        this = THIS;
        GF_VALIDATE_OR_GOTO ("glusterd", (this != NULL), out);

        conf = this->private;
        GF_VALIDATE_OR_GOTO (this->name, (conf != NULL), out);

        GF_VALIDATE_OR_GOTO (this->name, (dict != NULL), out);
        GF_VALIDATE_OR_GOTO (this->name, (prefix != NULL), out);
        GF_VALIDATE_OR_GOTO (this->name, (peerinfo != NULL), out);

        ret = glusterd_peerinfo_new (&new_peer, GD_FRIEND_STATE_DEFAULT, NULL,
                                     NULL, 0);
        if (ret) {
                gf_log (this->name, GF_LOG_ERROR, "Could not create peerinfo "
                        "object");
                goto out;
        }

        snprintf (key, sizeof (key), "%s.uuid", prefix);
        ret = dict_get_str (dict, key, &uuid_str);
        if (ret) {
                gf_log (this->name, GF_LOG_ERROR, "Key %s not present in "
                        "dictionary", key);
                goto out;
        }
        uuid_parse (uuid_str, new_peer->uuid);

        memset (key, 0, sizeof (key));
        snprintf (key, sizeof (key), "%s.hostname", prefix);
        ret = dict_get_str (dict, key, &hostname);
        if (ret) {
                gf_log (this->name, GF_LOG_ERROR, "Key %s not present in "
                        "dictionary", key);
                goto out;
        }
        ret = gd_add_address_to_peer (new_peer, hostname);
        if (ret) {
                gf_log (this->name, GF_LOG_ERROR,
                        "Could not add address to peer");
                goto out;
        }

        if (conf->op_version < GD_OP_VERSION_3_6_0) {
                ret = 0;
                goto cont;
        }

        memset (key, 0, sizeof (key));
        snprintf (key, sizeof (key), "%s.address-count", prefix);
        ret = dict_get_int32 (dict, key, &count);
        if (ret) {
                gf_log (this->name, GF_LOG_ERROR, "Key %s not present in "
                        "dictionary", key);
                goto out;
        }
        hostname = NULL;
        for (i = 0; i < count; i++) {
                memset (key, 0, sizeof (key));
                snprintf (key, sizeof (key), "%s.hostname%d",prefix, i);
                ret = dict_get_str (dict, key, &hostname);
                if (ret) {
                        gf_log (this->name, GF_LOG_ERROR, "Key %s not present "
                                "in dictionary", key);
                        goto out;
                }
                ret = gd_add_address_to_peer (new_peer, hostname);
                if (ret) {
                        gf_log (this->name, GF_LOG_ERROR,
                                "Could not add address to peer");
                        goto out;
                }

                hostname = NULL;
        }

cont:
        *peerinfo = new_peer;
        new_peer = NULL;
out:
        if (new_peer)
                glusterd_friend_cleanup (new_peer);

        gf_log (this ? this->name : "glusterd", GF_LOG_DEBUG, "Returning %d",
                ret);
        return ret;
}
