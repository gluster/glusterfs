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
#include "common-utils.h"

int32_t
glusterd_peerinfo_cleanup (glusterd_peerinfo_t *peerinfo)
{
        GF_ASSERT (peerinfo);
        glusterd_peerctx_t      *peerctx = NULL;
        gf_boolean_t            quorum_action = _gf_false;
        glusterd_conf_t         *priv = THIS->private;

        if (peerinfo->quorum_contrib != QUORUM_NONE)
                quorum_action = _gf_true;
        if (peerinfo->rpc) {
                peerctx = peerinfo->rpc->mydata;
                peerinfo->rpc->mydata = NULL;
                peerinfo->rpc = glusterd_rpc_clnt_unref (priv, peerinfo->rpc);
                peerinfo->rpc = NULL;
                if (peerctx) {
                        GF_FREE (peerctx->errstr);
                        GF_FREE (peerctx);
                }
        }
        glusterd_peerinfo_destroy (peerinfo);

        if (quorum_action)
                glusterd_do_quorum_action ();
        return 0;
}

int32_t
glusterd_peerinfo_destroy (glusterd_peerinfo_t *peerinfo)
{
        int32_t                         ret = -1;
        glusterd_peer_hostname_t *hostname = NULL;
        glusterd_peer_hostname_t *tmp = NULL;

        if (!peerinfo)
                goto out;

        list_del_init (&peerinfo->uuid_list);

        ret = glusterd_store_delete_peerinfo (peerinfo);
        if (ret) {
                gf_log ("glusterd", GF_LOG_ERROR, "Deleting peer info failed");
        }

        GF_FREE (peerinfo->hostname);
        peerinfo->hostname = NULL;

        list_for_each_entry_safe (hostname, tmp, &peerinfo->hostnames,
                                  hostname_list) {
                glusterd_peer_hostname_free (hostname);
        }

        glusterd_sm_tr_log_delete (&peerinfo->sm_log);
        GF_FREE (peerinfo);
        peerinfo = NULL;

        ret = 0;

out:
        return ret;
}

/* glusterd_peerinfo_find_by_hostname searches for a peer which matches the
 * hostname @hoststr and if found returns the pointer to peerinfo object.
 * Returns NULL otherwise.
 *
 * It first attempts a quick search by string matching @hoststr. If that fails,
 * it'll attempt a more thorough match by resolving the addresses and matching
 * the resolved addrinfos.
 */
glusterd_peerinfo_t *
glusterd_peerinfo_find_by_hostname (const char *hoststr)
{
        int                  ret      = -1;
        struct addrinfo     *addr     = NULL;
        struct addrinfo     *p        = NULL;
        xlator_t            *this     = NULL;
        glusterd_peerinfo_t *peerinfo = NULL;


        this = THIS;
        GF_ASSERT (hoststr);

        peerinfo = NULL;

        peerinfo = gd_peerinfo_find_from_hostname (hoststr);
        if (peerinfo)
                return peerinfo;

        ret = getaddrinfo (hoststr, NULL, NULL, &addr);
        if (ret != 0) {
                gf_log (this->name, GF_LOG_ERROR,
                        "error in getaddrinfo: %s\n",
                        gai_strerror(ret));
                goto out;
        }

        for (p = addr; p != NULL; p = p->ai_next) {
                peerinfo = gd_peerinfo_find_from_addrinfo (p);
                if (peerinfo) {
                        freeaddrinfo (addr);
                        return peerinfo;
                }
        }

out:
        gf_log (this->name, GF_LOG_DEBUG, "Unable to find friend: %s", hoststr);
        if (addr)
                freeaddrinfo (addr);
        return NULL;
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

        peerinfo = glusterd_peerinfo_find_by_hostname (hostname);
        if (peerinfo) {
                ret = 0;
                uuid_copy (uuid, peerinfo->uuid);
        } else {
                if (gf_is_local_addr (hostname)) {
                        uuid_copy (uuid, MY_UUID);
                        ret = 0;
                } else {
                        ret = -1;
                }
        }

        gf_log (this->name, GF_LOG_DEBUG, "returning %d", ret);
        return ret;
}

/* glusterd_peerinfo_find_by_uuid searches for a peer which matches the
 * uuid @uuid and if found returns the pointer to peerinfo object.
 * Returns NULL otherwise.
 */
glusterd_peerinfo_t *
glusterd_peerinfo_find_by_uuid (uuid_t uuid)
{
        glusterd_conf_t         *priv = NULL;
        glusterd_peerinfo_t     *entry = NULL;
        xlator_t                *this = NULL;

        this = THIS;
        GF_ASSERT (this);

        priv    = this->private;

        GF_ASSERT (priv);

        if (uuid_is_null (uuid))
                return NULL;

        list_for_each_entry (entry, &priv->peers, uuid_list) {
                if (!uuid_compare (entry->uuid, uuid)) {

                        gf_log (this->name, GF_LOG_DEBUG,
                                 "Friend found... state: %s",
                        glusterd_friend_sm_state_name_get (entry->state.state));
                        return entry;
                }
        }

        gf_log (this->name, GF_LOG_DEBUG, "Friend with uuid: %s, not found",
                uuid_utoa (uuid));
        return NULL;
}

/* glusterd_peerinfo_find will search for a peer matching either @uuid or
 * @hostname and return a pointer to the peerinfo object
 * Returns NULL otherwise.
 */
glusterd_peerinfo_t *
glusterd_peerinfo_find (uuid_t uuid, const char *hostname)
{
        glusterd_peerinfo_t *peerinfo = NULL;
        xlator_t            *this     = NULL;

        this = THIS;
        GF_ASSERT (this);


        if (uuid) {
                peerinfo = glusterd_peerinfo_find_by_uuid (uuid);

                if (peerinfo) {
                        return peerinfo;
                } else {
                        gf_log (this->name, GF_LOG_DEBUG,
                                 "Unable to find peer by uuid: %s",
                                 uuid_utoa (uuid));
                }

        }

        if (hostname) {
                peerinfo = glusterd_peerinfo_find_by_hostname (hostname);

                if (peerinfo) {
                        return peerinfo;
                } else {
                        gf_log (this->name, GF_LOG_DEBUG,
                                "Unable to find hostname: %s", hostname);
                }
        }
        return NULL;
}

/* glusterd_peerinfo_new will create a new peerinfo object and set it's members
 * values using the passed parameters.
 * @hostname is added as the first entry in peerinfo->hostnames list and also
 * set to peerinfo->hostname.
 * It returns a pointer to peerinfo object if successfull and returns NULL
 * otherwise. The caller should take care of freeing the created peerinfo
 * object.
 */
glusterd_peerinfo_t *
glusterd_peerinfo_new (glusterd_friend_sm_state_t state, uuid_t *uuid,
                       const char *hostname, int port)
{
        glusterd_peerinfo_t      *new_peer = NULL;
        int                      ret = -1;

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
                /* Also set it to peerinfo->hostname. Doing this as we use
                 * peerinfo->hostname in a lot of places and is really hard to
                 * get everything right
                 */
                new_peer->hostname = gf_strdup (hostname);
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
out:
        if (ret && new_peer) {
                glusterd_peerinfo_cleanup (new_peer);
                new_peer = NULL;
        }
        return new_peer;
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
        gf_log ("glusterd", GF_LOG_DEBUG, "Returning %d", ret);
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
        gf_log ("glusterd", GF_LOG_DEBUG, "Returning %d", ret);
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
        count = 0;
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

/* gd_peerinfo_find_from_hostname iterates over all the addresses saved for each
 * peer and matches it to @hoststr.
 * Returns the matched peer if found else returns NULL
 */
glusterd_peerinfo_t *
gd_peerinfo_find_from_hostname (const char *hoststr)
{
        xlator_t                 *this    = NULL;
        glusterd_conf_t          *priv    = NULL;
        glusterd_peerinfo_t      *peer    = NULL;
        glusterd_peer_hostname_t *tmphost = NULL;

        this = THIS;
        GF_ASSERT (this != NULL);
        priv = this->private;
        GF_VALIDATE_OR_GOTO (this->name, (priv != NULL), out);

        GF_VALIDATE_OR_GOTO (this->name, (hoststr != NULL), out);

        list_for_each_entry (peer, &priv->peers, uuid_list) {
                list_for_each_entry (tmphost, &peer->hostnames,hostname_list) {
                        if (!strncasecmp (tmphost->hostname, hoststr, 1024)) {
                                gf_log (this->name, GF_LOG_DEBUG,
                                        "Friend %s found.. state: %d",
                                        tmphost->hostname, peer->state.state);
                                return peer;
                        }
                }
        }
out:
        return NULL;
}

/* gd_peerinfo_find_from_addrinfo iterates over all the addresses saved for each
 * peer, resolves them and compares them to @addr.
 *
 *
 * NOTE: As getaddrinfo is a blocking call and is being performed multiple times
 * in this function, it could lead to the calling thread to be blocked for
 * significant amounts of time.
 *
 * Returns the matched peer if found else returns NULL
 */
glusterd_peerinfo_t *
gd_peerinfo_find_from_addrinfo (const struct addrinfo *addr)
{
        xlator_t                 *this    = NULL;
        glusterd_conf_t          *conf    = NULL;
        glusterd_peerinfo_t      *peer    = NULL;
        glusterd_peer_hostname_t *address = NULL;
        int                       ret     = 0;
        struct addrinfo          *paddr   = NULL;
        struct addrinfo          *tmp     = NULL;

        this = THIS;
        GF_ASSERT (this != NULL);
        conf = this->private;
        GF_VALIDATE_OR_GOTO (this->name, (conf != NULL), out);

        GF_VALIDATE_OR_GOTO (this->name, (addr != NULL), out);

        list_for_each_entry (peer, &conf->peers, uuid_list) {
                list_for_each_entry (address, &peer->hostnames, hostname_list) {
                        /* TODO: Cache the resolved addrinfos to improve
                         * performance
                         */
                        ret = getaddrinfo (address->hostname, NULL, NULL,
                                           &paddr);
                        if (ret) {
                                /* Don't fail if getaddrinfo fails, continue
                                 * onto the next address
                                 */
                                gf_log (this->name, GF_LOG_TRACE,
                                        "getaddrinfo for %s failed (%s)",
                                        address->hostname, gai_strerror (ret));
                                ret = 0;
                                continue;
                        }

                        for (tmp = paddr; tmp != NULL; tmp = tmp->ai_next) {
                                if (gf_compare_sockaddr (addr->ai_addr,
                                                         tmp->ai_addr)) {
                                        freeaddrinfo (paddr);
                                        return peer;
                                }
                        }
                }
        }
out:
        return NULL;
}

/* gd_update_peerinfo_from_dict will update the hostnames for @peerinfo from
 * peer details with @prefix in @dict.
 * Returns 0 on sucess and -1 on failure.
 */
int
gd_update_peerinfo_from_dict (glusterd_peerinfo_t *peerinfo, dict_t *dict,
                              const char *prefix)
{
        int                  ret      = -1;
        xlator_t            *this     = NULL;
        glusterd_conf_t     *conf     = NULL;
        char                 key[100] = {0,};
        char                *hostname = NULL;
        int                  count    = 0;
        int                  i        = 0;

        this = THIS;
        GF_ASSERT (this != NULL);

        conf = this->private;
        GF_VALIDATE_OR_GOTO (this->name, (conf != NULL), out);

        GF_VALIDATE_OR_GOTO (this->name, (peerinfo != NULL), out);
        GF_VALIDATE_OR_GOTO (this->name, (dict != NULL), out);
        GF_VALIDATE_OR_GOTO (this->name, (prefix != NULL), out);

        memset (key, 0, sizeof (key));
        snprintf (key, sizeof (key), "%s.hostname", prefix);
        ret = dict_get_str (dict, key, &hostname);
        if (ret) {
                gf_log (this->name, GF_LOG_ERROR, "Key %s not present in "
                        "dictionary", key);
                goto out;
        }
        ret = gd_add_address_to_peer (peerinfo, hostname);
        if (ret) {
                gf_log (this->name, GF_LOG_ERROR,
                        "Could not add address to peer");
                goto out;
        }
        /* Also set peerinfo->hostname to the first address */
        if (peerinfo->hostname != NULL)
                GF_FREE (peerinfo->hostname);
        peerinfo->hostname = gf_strdup (hostname);

        if (conf->op_version < GD_OP_VERSION_3_6_0) {
                ret = 0;
                goto out;
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
                ret = gd_add_address_to_peer (peerinfo, hostname);
                if (ret) {
                        gf_log (this->name, GF_LOG_ERROR,
                                "Could not add address to peer");
                        goto out;
                }

                hostname = NULL;
        }

out:
        gf_log (this->name, GF_LOG_DEBUG, "Returning %d", ret);
        return ret;
}

/* gd_peerinfo_from_dict creates a peerinfo object from details of peer with
 * @prefix in @dict.
 * Returns a pointer to the created peerinfo object on success, and NULL on
 * failure.
 */
glusterd_peerinfo_t *
gd_peerinfo_from_dict (dict_t *dict, const char *prefix)
{
        int                  ret      = -1;
        xlator_t            *this     = NULL;
        glusterd_conf_t     *conf     = NULL;
        glusterd_peerinfo_t *new_peer = NULL;
        char                 key[100] = {0,};
        char                *uuid_str = NULL;

        this = THIS;
        GF_VALIDATE_OR_GOTO ("glusterd", (this != NULL), out);

        conf = this->private;
        GF_VALIDATE_OR_GOTO (this->name, (conf != NULL), out);

        GF_VALIDATE_OR_GOTO (this->name, (dict != NULL), out);
        GF_VALIDATE_OR_GOTO (this->name, (prefix != NULL), out);

        new_peer = glusterd_peerinfo_new (GD_FRIEND_STATE_DEFAULT, NULL, NULL,
                                          0);
        if (new_peer == NULL) {
                ret = -1;
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

        ret = gd_update_peerinfo_from_dict (new_peer, dict, prefix);

out:
        if ((ret != 0) && (new_peer != NULL)) {
                glusterd_peerinfo_cleanup (new_peer);
                new_peer = NULL;
        }

        return new_peer;
}

int
gd_add_peer_hostnames_to_dict (glusterd_peerinfo_t *peerinfo, dict_t *dict,
                               const char *prefix)
{
        int                       ret      = -1;
        xlator_t                 *this     = NULL;
        glusterd_conf_t          *conf     = NULL;
        char                      key[256] = {0,};
        glusterd_peer_hostname_t *addr     = NULL;
        int                       count    = 0;

        this = THIS;
        GF_ASSERT (this != NULL);

        conf = this->private;
        GF_VALIDATE_OR_GOTO (this->name, (conf != NULL), out);

        if (conf->op_version < GD_OP_VERSION_3_6_0) {
                ret = 0;
                goto out;
        }

        GF_VALIDATE_OR_GOTO (this->name, (peerinfo != NULL), out);
        GF_VALIDATE_OR_GOTO (this->name, (dict != NULL), out);
        GF_VALIDATE_OR_GOTO (this->name, (prefix != NULL), out);

        list_for_each_entry (addr, &peerinfo->hostnames, hostname_list) {
                memset (key, 0, sizeof (key));
                snprintf (key, sizeof (key), "%s.hostname%d", prefix, count);
                ret = dict_set_dynstr_with_alloc (dict, key, addr->hostname);
                if (ret)
                        goto out;
                count++;
        }

        memset (key, 0, sizeof (key));
        snprintf (key, sizeof (key), "%s.hostname_count", prefix);
        ret = dict_set_int32 (dict, key, count);

out:
        return ret;
}

int
gd_add_peer_detail_to_dict (glusterd_peerinfo_t *peerinfo, dict_t *friends,
                            int count)
{

        int             ret = -1;
        char            key[256] = {0, };
        char           *peer_uuid_str = NULL;

        GF_ASSERT (peerinfo);
        GF_ASSERT (friends);

        snprintf (key, sizeof (key), "friend%d.uuid", count);
        peer_uuid_str = gd_peer_uuid_str (peerinfo);
        ret = dict_set_str (friends, key, peer_uuid_str);
        if (ret)
                goto out;

        memset (key, 0, sizeof (key));
        snprintf (key, sizeof (key), "friend%d.hostname", count);
        ret = dict_set_str (friends, key, peerinfo->hostname);
        if (ret)
                goto out;

        memset (key, 0, sizeof (key));
        snprintf (key, sizeof (key), "friend%d.port", count);
        ret = dict_set_int32 (friends, key, peerinfo->port);
        if (ret)
                goto out;

        memset (key, 0, sizeof (key));
        snprintf (key, sizeof (key), "friend%d.stateId", count);
        ret = dict_set_int32 (friends, key, peerinfo->state.state);
        if (ret)
                goto out;

        memset (key, 0, sizeof (key));
        snprintf (key, sizeof (key), "friend%d.state", count);
        ret = dict_set_str (friends, key,
                    glusterd_friend_sm_state_name_get(peerinfo->state.state));
        if (ret)
                goto out;

        memset (key, 0, sizeof (key));
        snprintf (key, sizeof (key), "friend%d.connected", count);
        ret = dict_set_int32 (friends, key, (int32_t)peerinfo->connected);
        if (ret)
                goto out;

        memset (key, 0, sizeof (key));
        snprintf (key, sizeof (key), "friend%d", count);
        ret = gd_add_peer_hostnames_to_dict (peerinfo, friends, key);

out:
        return ret;
}
