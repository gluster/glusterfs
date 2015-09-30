/*
  Copyright (c) 2013 Red Hat, Inc. <http://www.redhat.com>
  This file is part of GlusterFS.

  This file is licensed to you under your choice of the GNU Lesser
  General Public License, version 3 or any later version (LGPLv3 or
  later), or the GNU General Public License, version 2 (GPLv2), in all
  cases as published by the Free Software Foundation.
*/

#include "rpcsvc.h"
#ifndef RPC_DRC_H
#include "rpc-drc.h"
#endif
#include "locking.h"
#include "hashfn.h"
#include "common-utils.h"
#include "statedump.h"
#include "mem-pool.h"

#include <netinet/in.h>
#include <unistd.h>

/**
 * rpcsvc_drc_op_destroy - Destroys the cached reply
 *
 * @param drc - the main drc structure
 * @param reply - the cached reply to destroy
 * @return NULL if reply is destroyed, reply otherwise
 */
static drc_cached_op_t *
rpcsvc_drc_op_destroy (rpcsvc_drc_globals_t *drc, drc_cached_op_t *reply)
{
        GF_ASSERT (drc);
        GF_ASSERT (reply);

        if (reply->state == DRC_OP_IN_TRANSIT)
                return reply;

        iobref_unref (reply->msg.iobref);
        if (reply->msg.rpchdr)
                GF_FREE (reply->msg.rpchdr);
        if (reply->msg.proghdr)
                GF_FREE (reply->msg.proghdr);
        if (reply->msg.progpayload)
                GF_FREE (reply->msg.progpayload);

        list_del (&reply->global_list);
        reply->client->op_count--;
        drc->op_count--;
        mem_put (reply);
        reply = NULL;

        return reply;
}

/**
 * rpcsvc_drc_op_rb_unref - This function is used in rb tree cleanup only
 *
 * @param reply - the cached reply to unref
 * @param drc - the main drc structure
 * @return void
 */
static void
rpcsvc_drc_rb_op_destroy (void *reply, void *drc)
{
        rpcsvc_drc_op_destroy (drc, (drc_cached_op_t *)reply);
}

/**
 * rpcsvc_remove_drc_client - Cleanup the drc client
 *
 * @param client - the drc client to be removed
 * @return void
 */
static void
rpcsvc_remove_drc_client (drc_client_t *client)
{
        rb_destroy (client->rbtree, rpcsvc_drc_rb_op_destroy);
        list_del (&client->client_list);
        GF_FREE (client);
}

/**
 * rpcsvc_client_lookup - Given a sockaddr_storage, find the client if it exists
 *
 * @param drc - the main drc structure
 * @param sockaddr - the network address of the client to be looked up
 * @return drc client if it exists, NULL otherwise
 */
static drc_client_t *
rpcsvc_client_lookup (rpcsvc_drc_globals_t *drc,
                      struct sockaddr_storage *sockaddr)
{
        drc_client_t    *client = NULL;

        GF_ASSERT (drc);
        GF_ASSERT (sockaddr);

        if (list_empty (&drc->clients_head))
            return NULL;

        list_for_each_entry (client, &drc->clients_head, client_list) {
                if (gf_sock_union_equal_addr (&client->sock_union,
                                              (union gf_sock_union *)sockaddr))
                        return client;
        }

        return NULL;
}

/**
 * drc_compare_reqs - Used by rbtree to determine if incoming req matches with
 *                    an existing node(cached reply) in rbtree
 *
 * @param item - pointer to the incoming req
 * @param rb_node_data - pointer to an rbtree node (cached reply)
 * @param param - drc pointer - unused here, but used in *op_destroy
 * @return 0 if req matches reply, else (req->xid - reply->xid)
 */
int
drc_compare_reqs (const void *item, const void *rb_node_data, void *param)
{
        int               ret      = -1;
        drc_cached_op_t  *req      = NULL;
        drc_cached_op_t  *reply    = NULL;

        GF_ASSERT (item);
        GF_ASSERT (rb_node_data);
        GF_ASSERT (param);

        req = (drc_cached_op_t *)item;
        reply = (drc_cached_op_t *)rb_node_data;

        ret = req->xid - reply->xid;
        if (ret != 0)
                return ret;

        if (req->prognum == reply->prognum &&
            req->procnum == reply->procnum &&
            req->progversion == reply->progversion)
                return 0;

        return 1;
}

/**
 * drc_init_client_cache - initialize a drc client and its rb tree
 *
 * @param drc - the main drc structure
 * @param client - the drc client to be initialized
 * @return 0 on success, -1 on failure
 */
static int
drc_init_client_cache (rpcsvc_drc_globals_t *drc, drc_client_t *client)
{
        GF_ASSERT (drc);
        GF_ASSERT (client);

        client->rbtree = rb_create (drc_compare_reqs, drc, NULL);
        if (!client->rbtree) {
                gf_log (GF_RPCSVC, GF_LOG_DEBUG, "rb tree creation failed");
                return -1;
        }

        return 0;
}

/**
 * rpcsvc_get_drc_client - find the drc client with given sockaddr, else
 *                         allocate and initialize a new drc client
 *
 * @param drc - the main drc structure
 * @param sockaddr - network address of client
 * @return drc client on success, NULL on failure
 */
static drc_client_t *
rpcsvc_get_drc_client (rpcsvc_drc_globals_t *drc,
                       struct sockaddr_storage *sockaddr)
{
        drc_client_t      *client      = NULL;

        GF_ASSERT (drc);
        GF_ASSERT (sockaddr);

        client = rpcsvc_client_lookup (drc, sockaddr);
        if (client)
                goto out;

        /* if lookup fails, allocate cache for the new client */
        client = GF_CALLOC (1, sizeof (drc_client_t),
                            gf_common_mt_drc_client_t);
        if (!client)
                goto out;

        client->ref = 0;
        client->sock_union = (union gf_sock_union)*sockaddr;
        client->op_count = 0;
        INIT_LIST_HEAD (&client->client_list);

        if (drc_init_client_cache (drc, client)) {
                gf_log (GF_RPCSVC, GF_LOG_DEBUG,
                        "initialization of drc client failed");
                GF_FREE (client);
                client = NULL;
                goto out;
        }
        drc->client_count++;

        list_add (&client->client_list, &drc->clients_head);

 out:
        return client;
}

/**
 * rpcsvc_need_drc - Determine if a request needs DRC service
 *
 * @param req - incoming request
 * @return 1 if DRC is needed for req, 0 otherwise
 */
int
rpcsvc_need_drc (rpcsvc_request_t *req)
{
        rpcsvc_actor_t           *actor = NULL;
        rpcsvc_drc_globals_t     *drc   = NULL;

        GF_ASSERT (req);
        GF_ASSERT (req->svc);

        drc = req->svc->drc;

        if (!drc || drc->status == DRC_UNINITIATED)
                return 0;

        actor = rpcsvc_program_actor (req);
        if (!actor)
                return 0;

        return (actor->op_type == DRC_NON_IDEMPOTENT
                && drc->type != DRC_TYPE_NONE);
}

/**
 * rpcsvc_drc_client_ref - ref the drc client
 *
 * @param client - the drc client to ref
 * @return client
 */
static drc_client_t *
rpcsvc_drc_client_ref (drc_client_t *client)
{
        GF_ASSERT (client);
        client->ref++;
        return client;
}

/**
 * rpcsvc_drc_client_unref - unref the drc client, and destroy
 *                           the client on last unref
 *
 * @param drc - the main drc structure
 * @param client - the drc client to unref
 * @return NULL if it is the last unref, client otherwise
 */
static drc_client_t *
rpcsvc_drc_client_unref (rpcsvc_drc_globals_t *drc, drc_client_t *client)
{
        GF_ASSERT (drc);
        GF_ASSERT (client->ref);

        client->ref--;
        if (!client->ref) {
                drc->client_count--;
                rpcsvc_remove_drc_client (client);
                client = NULL;
        }

        return client;
}

/**
 * rpcsvc_drc_lookup - lookup a request to see if it is already cached
 *
 * @param req - incoming request
 * @return cached reply of req if found, NULL otherwise
 */
drc_cached_op_t *
rpcsvc_drc_lookup (rpcsvc_request_t *req)
{
        drc_client_t           *client = NULL;
        drc_cached_op_t        *reply  = NULL;
        drc_cached_op_t        new = {
                .xid            = req->xid,
                .prognum        = req->prognum,
                .progversion    = req->progver,
                .procnum        = req->procnum,
        };

        GF_ASSERT (req);

        if (!req->trans->drc_client) {
                client = rpcsvc_get_drc_client (req->svc->drc,
                                                &req->trans->peerinfo.sockaddr);
                if (!client)
                        goto out;

                req->trans->drc_client
                        = rpcsvc_drc_client_ref (client);
        }

        client = req->trans->drc_client;

        if (client->op_count == 0)
                goto out;

        reply = rb_find (client->rbtree, &new);

 out:
        return reply;
}

/**
 * rpcsvc_send_cached_reply - send the cached reply for the incoming request
 *
 * @param req - incoming request (which is a duplicate in this case)
 * @param reply - the cached reply for req
 * @return 0 on successful reply submission, -1 or other non-zero value otherwise
 */
int
rpcsvc_send_cached_reply (rpcsvc_request_t *req, drc_cached_op_t *reply)
{
        int     ret = 0;

        GF_ASSERT (req);
        GF_ASSERT (reply);

        gf_log (GF_RPCSVC, GF_LOG_DEBUG, "sending cached reply: xid: %d, "
                "client: %s", req->xid, req->trans->peerinfo.identifier);

        rpcsvc_drc_client_ref (reply->client);
        ret = rpcsvc_transport_submit (req->trans,
                     reply->msg.rpchdr, reply->msg.rpchdrcount,
                     reply->msg.proghdr, reply->msg.proghdrcount,
                     reply->msg.progpayload, reply->msg.progpayloadcount,
                     reply->msg.iobref, req->trans_private);
        rpcsvc_drc_client_unref (req->svc->drc, reply->client);

        return ret;
}

/**
 * rpcsvc_cache_reply - cache the reply for the processed request 'req'
 *
 * @param req - processed request
 * @param iobref - iobref structure of the reply
 * @param rpchdr - rpc header of the reply
 * @param rpchdrcount - size of rpchdr
 * @param proghdr - program header of the reply
 * @param proghdrcount - size of proghdr
 * @param payload - payload of the reply if any
 * @param payloadcount - size of payload
 * @return 0 on success, -1 on failure
 */
int
rpcsvc_cache_reply (rpcsvc_request_t *req, struct iobref *iobref,
                    struct iovec *rpchdr, int rpchdrcount,
                    struct iovec *proghdr, int proghdrcount,
                    struct iovec *payload, int payloadcount)
{
        int                       ret              = -1;
        drc_cached_op_t          *reply            = NULL;

        GF_ASSERT (req);
        GF_ASSERT (req->reply);

        reply = req->reply;

        reply->state = DRC_OP_CACHED;

        reply->msg.iobref = iobref_ref (iobref);

        reply->msg.rpchdrcount = rpchdrcount;
        reply->msg.rpchdr = iov_dup (rpchdr, rpchdrcount);

        reply->msg.proghdrcount = proghdrcount;
        reply->msg.proghdr = iov_dup (proghdr, proghdrcount);

        reply->msg.progpayloadcount = payloadcount;
        if (payloadcount)
                reply->msg.progpayload = iov_dup (payload, payloadcount);

        //        rpcsvc_drc_client_unref (req->svc->drc, req->trans->drc_client);
        //        rpcsvc_drc_op_unref (req->svc->drc, reply);
        ret = 0;

        return ret;
}

/**
 * rpcsvc_vacate_drc_entries - free up some percentage of drc cache
 *                             based on the lru factor
 *
 * @param drc - the main drc structure
 * @return void
 */
static void
rpcsvc_vacate_drc_entries (rpcsvc_drc_globals_t *drc)
{
        uint32_t            i           = 0;
        uint32_t            n           = 0;
        drc_cached_op_t    *reply       = NULL;
        drc_cached_op_t    *tmp         = NULL;
        drc_client_t       *client      = NULL;

        GF_ASSERT (drc);

        n = drc->global_cache_size / drc->lru_factor;

        list_for_each_entry_safe_reverse (reply, tmp, &drc->cache_head, global_list) {
                /* Don't delete ops that are in transit */
                if (reply->state == DRC_OP_IN_TRANSIT)
                        continue;

                client = reply->client;

                rb_delete (client->rbtree, reply);

                rpcsvc_drc_op_destroy (drc, reply);
                rpcsvc_drc_client_unref (drc, client);
                i++;
                if (i >= n)
                        break;
        }
}

/**
 * rpcsvc_add_op_to_cache - insert the cached op into the client rbtree and drc list
 *
 * @param drc - the main drc structure
 * @param reply - the op to be inserted
 * @return 0 on success, -1 on failure
 */
static int
rpcsvc_add_op_to_cache (rpcsvc_drc_globals_t *drc, drc_cached_op_t *reply)
{
        drc_client_t        *client         = NULL;
        drc_cached_op_t    **tmp_reply      = NULL;

        GF_ASSERT (drc);
        GF_ASSERT (reply);

        client = reply->client;

        /* cache is full, free up some space */
        if (drc->op_count >= drc->global_cache_size)
                rpcsvc_vacate_drc_entries (drc);

        tmp_reply = (drc_cached_op_t **)rb_probe (client->rbtree, reply);
        if (!tmp_reply) {
                /* mem alloc failed */
                return -1;
        } else if (*tmp_reply != reply) {
                /* should never happen */
                gf_log (GF_RPCSVC, GF_LOG_ERROR,
                        "DRC failed to detect duplicates");
                return -1;
        }

        client->op_count++;
        list_add (&reply->global_list, &drc->cache_head);
        drc->op_count++;

        return 0;
}

/**
 * rpcsvc_cache_request - cache the in-transition incoming request
 *
 * @param req - incoming request
 * @return 0 on success, -1 on failure
 */
int
rpcsvc_cache_request (rpcsvc_request_t *req)
{
        int                        ret            = -1;
        drc_client_t              *client         = NULL;
        drc_cached_op_t           *reply          = NULL;
        rpcsvc_drc_globals_t      *drc            = NULL;

        GF_ASSERT (req);

        drc = req->svc->drc;

        client = req->trans->drc_client;
        if (!client) {
                gf_log (GF_RPCSVC, GF_LOG_DEBUG, "drc client is NULL");
                goto out;
        }

        reply = mem_get0 (drc->mempool);
        if (!reply)
                goto out;

        reply->client = rpcsvc_drc_client_ref (client);
        reply->xid = req->xid;
        reply->prognum = req->prognum;
        reply->progversion = req->progver;
        reply->procnum = req->procnum;
        reply->state = DRC_OP_IN_TRANSIT;
        req->reply = reply;
        INIT_LIST_HEAD (&reply->global_list);

        ret = rpcsvc_add_op_to_cache (drc, reply);
        if (ret) {
                req->reply = NULL;
                rpcsvc_drc_op_destroy (drc, reply);
                rpcsvc_drc_client_unref (drc, client);
                gf_log (GF_RPCSVC, GF_LOG_DEBUG, "Failed to add op to drc cache");
        }

 out:
        return ret;
}

/**
 *
 * rpcsvc_drc_priv - function which dumps the drc state
 *
 * @param drc - the main drc structure
 * @return 0 on success, -1 on failure
 */
int32_t
rpcsvc_drc_priv (rpcsvc_drc_globals_t *drc)
{
        int                      i                         = 0;
        char                     key[GF_DUMP_MAX_BUF_LEN]  = {0};
        drc_client_t            *client                    = NULL;
        char                     ip[INET6_ADDRSTRLEN]      = {0};

        if (!drc || drc->status == DRC_UNINITIATED) {
                gf_log (GF_RPCSVC, GF_LOG_DEBUG, "DRC is "
                        "uninitialized, not dumping its state");
                return 0;
        }

        gf_proc_dump_add_section("rpc.drc");

        if (TRY_LOCK (&drc->lock))
                return -1;

        gf_proc_dump_build_key (key, "drc", "type");
        gf_proc_dump_write (key, "%d", drc->type);

        gf_proc_dump_build_key (key, "drc", "client_count");
        gf_proc_dump_write (key, "%d", drc->client_count);

        gf_proc_dump_build_key (key, "drc", "current_cache_size");
        gf_proc_dump_write (key, "%d", drc->op_count);

        gf_proc_dump_build_key (key, "drc", "max_cache_size");
        gf_proc_dump_write (key, "%d", drc->global_cache_size);

        gf_proc_dump_build_key (key, "drc", "lru_factor");
        gf_proc_dump_write (key, "%d", drc->lru_factor);

        gf_proc_dump_build_key (key, "drc", "duplicate_request_count");
        gf_proc_dump_write (key, "%d", drc->cache_hits);

        gf_proc_dump_build_key (key, "drc", "in_transit_duplicate_requests");
        gf_proc_dump_write (key, "%d", drc->intransit_hits);

        list_for_each_entry (client, &drc->clients_head, client_list) {
                gf_proc_dump_build_key (key, "client", "%d.ip-address", i);
                memset (ip, 0, INET6_ADDRSTRLEN);
                switch (client->sock_union.storage.ss_family) {
                case AF_INET:
                        gf_proc_dump_write (key, "%s", inet_ntop (AF_INET,
                                &client->sock_union.sin.sin_addr.s_addr,
                                ip, INET_ADDRSTRLEN));
                        break;
                case AF_INET6:
                        gf_proc_dump_write (key, "%s", inet_ntop (AF_INET6,
                                &client->sock_union.sin6.sin6_addr,
                                ip, INET6_ADDRSTRLEN));
                        break;
                default:
                        gf_proc_dump_write (key, "%s", "N/A");
                }

                gf_proc_dump_build_key (key, "client", "%d.ref_count", i);
                gf_proc_dump_write (key, "%d", client->ref);
                gf_proc_dump_build_key (key, "client", "%d.op_count", i);
                gf_proc_dump_write (key, "%d", client->op_count);
                i++;
        }

        UNLOCK (&drc->lock);
        return 0;
}

/**
 * rpcsvc_drc_notify - function which is notified of RPC transport events
 *
 * @param svc - pointer to rpcsvc_t structure of the rpc
 * @param xl - pointer to the xlator
 * @param event - the event which triggered this notify
 * @param data - the transport structure
 * @return 0 on success, -1 on failure
 */
int
rpcsvc_drc_notify (rpcsvc_t *svc, void *xl,
                   rpcsvc_event_t event, void *data)
{
        int                       ret          = -1;
        rpc_transport_t          *trans        = NULL;
        drc_client_t             *client       = NULL;
        rpcsvc_drc_globals_t     *drc          = NULL;

        GF_ASSERT (svc);
        GF_ASSERT (svc->drc);
        GF_ASSERT (data);

        drc = svc->drc;

        if (drc->status == DRC_UNINITIATED ||
            drc->type == DRC_TYPE_NONE)
                return 0;

        LOCK (&drc->lock);
        {
                trans = (rpc_transport_t *)data;
                client = rpcsvc_get_drc_client (drc, &trans->peerinfo.sockaddr);
                if (!client)
                        goto unlock;

                switch (event) {
                case RPCSVC_EVENT_ACCEPT:
                        trans->drc_client = rpcsvc_drc_client_ref (client);
                        ret = 0;
                        break;

                case RPCSVC_EVENT_DISCONNECT:
                        ret = 0;
                        if (list_empty (&drc->clients_head))
                                break;
                        /* should be the last unref */
                        trans->drc_client = NULL;
                        rpcsvc_drc_client_unref (drc, client);
                        break;

                default:
                        break;
                }
        }
unlock:
        UNLOCK (&drc->lock);
        return ret;
}

/**
 * rpcsvc_drc_init - Initialize the duplicate request cache service
 *
 * @param svc - pointer to rpcsvc_t structure of the rpc
 * @param options - the options dictionary which configures drc
 * @return 0 on success, non-zero integer on failure
 */
int
rpcsvc_drc_init (rpcsvc_t *svc, dict_t *options)
{
        int                         ret            = 0;
        uint32_t                    drc_type       = 0;
        uint32_t                    drc_size       = 0;
        uint32_t                    drc_factor     = 0;
        rpcsvc_drc_globals_t       *drc            = NULL;

        GF_ASSERT (svc);
        GF_ASSERT (options);

        /* Toggle DRC on/off, when more drc types(persistent/cluster)
         * are added, we shouldn't treat this as boolean. */
        ret = dict_get_str_boolean (options, "nfs.drc", _gf_false);
        if (ret == -1) {
                gf_log (GF_RPCSVC, GF_LOG_INFO,
                        "drc user options need second look");
                ret = _gf_false;
        }

        gf_log (GF_RPCSVC, GF_LOG_INFO, "DRC is turned %s", (ret?"ON":"OFF"));

        /*DRC off, nothing to do */
        if (ret == _gf_false)
                return (0);

        drc = GF_CALLOC (1, sizeof (rpcsvc_drc_globals_t),
                         gf_common_mt_drc_globals_t);
        if (!drc)
                return (-1);

        LOCK_INIT (&drc->lock);
        svc->drc = drc;

        LOCK (&drc->lock);

        /* Specify type of DRC to be used */
        ret = dict_get_uint32 (options, "nfs.drc-type", &drc_type);
        if (ret) {
                gf_log (GF_RPCSVC, GF_LOG_DEBUG, "drc type not set."
                        " Continuing with default");
                drc_type = DRC_DEFAULT_TYPE;
        }

        drc->type = drc_type;

        /* Set the global cache size (no. of ops to cache) */
        ret = dict_get_uint32 (options, "nfs.drc-size", &drc_size);
        if (ret) {
                gf_log (GF_RPCSVC, GF_LOG_DEBUG, "drc size not set."
                        " Continuing with default size");
                drc_size = DRC_DEFAULT_CACHE_SIZE;
        }

        drc->global_cache_size = drc_size;

        /* Mempool for cached ops */
        drc->mempool = mem_pool_new (drc_cached_op_t, drc->global_cache_size);
        if (!drc->mempool) {
                gf_log (GF_RPCSVC, GF_LOG_ERROR, "Failed to get mempool for"
                        " DRC, drc-size: %d", drc->global_cache_size);
                ret = -1;
                goto out;
        }

        /* What percent of cache to be evicted whenever it fills up */
        ret = dict_get_uint32 (options, "nfs.drc-lru-factor", &drc_factor);
        if (ret) {
                gf_log (GF_RPCSVC, GF_LOG_DEBUG, "drc lru factor not set."
                        " Continuing with policy default");
                drc_factor = DRC_DEFAULT_LRU_FACTOR;
        }

        drc->lru_factor = (drc_lru_factor_t) drc_factor;

        INIT_LIST_HEAD (&drc->clients_head);
        INIT_LIST_HEAD (&drc->cache_head);

        ret = rpcsvc_register_notify (svc, rpcsvc_drc_notify, THIS);
        if (ret) {
                gf_log (GF_RPCSVC, GF_LOG_ERROR,
                        "registration of drc_notify function failed");
                goto out;
        }

        gf_log (GF_RPCSVC, GF_LOG_DEBUG, "drc init successful");
        drc->status = DRC_INITIATED;
 out:
        UNLOCK (&drc->lock);
        if (ret == -1) {
                if (drc->mempool) {
                        mem_pool_destroy (drc->mempool);
                        drc->mempool = NULL;
                }
                GF_FREE (drc);
                svc->drc = NULL;
        }
        return ret;
}

int
rpcsvc_drc_deinit (rpcsvc_t *svc)
{
        rpcsvc_drc_globals_t *drc  = NULL;

        if (!svc)
                return (-1);

        drc = svc->drc;
        if (!drc)
                return (0);

        LOCK (&drc->lock);
        (void) rpcsvc_unregister_notify (svc, rpcsvc_drc_notify, THIS);
        if (drc->mempool) {
                mem_pool_destroy (drc->mempool);
                drc->mempool = NULL;
        }
        UNLOCK (&drc->lock);

        GF_FREE (drc);
        svc->drc = NULL;

        return (0);
}

int
rpcsvc_drc_reconfigure (rpcsvc_t *svc, dict_t *options)
{
        int                     ret        = -1;
        gf_boolean_t            enable_drc = _gf_false;
        rpcsvc_drc_globals_t    *drc       = NULL;
        uint32_t                drc_size   = 0;

        /* Input sanitization */
        if ((!svc) || (!options))
                return (-1);

        /* If DRC was not enabled before, Let rpcsvc_drc_init() to
         * take care of DRC initialization part.
         */
        drc = svc->drc;
        if (!drc) {
                return rpcsvc_drc_init(svc, options);
        }

        /* DRC was already enabled before. Going to be reconfigured. Check
         * if reconfigured options contain "nfs.drc" and "nfs.drc-size".
         *
         * NB: If DRC is "OFF", "drc-size" has no role to play.
         *     So, "drc-size" gets evaluated IFF DRC is "ON".
         *
         * If DRC is reconfigured,
         *     case 1: DRC is "ON"
         *         sub-case 1: drc-size remains same
         *              ACTION: Nothing to do.
         *         sub-case 2: drc-size just changed
         *              ACTION: rpcsvc_drc_deinit() followed by
         *                      rpcsvc_drc_init().
         *
         *     case 2: DRC is "OFF"
         *         ACTION: rpcsvc_drc_deinit()
         */
        ret = dict_get_str_boolean (options, "nfs.drc", _gf_false);
        if (ret < 0)
                ret = _gf_false;

        enable_drc = ret;
        gf_log (GF_RPCSVC, GF_LOG_INFO, "DRC is turned %s", (ret?"ON":"OFF"));

        /* case 1: DRC is "ON"*/
        if (enable_drc) {
                /* Fetch drc-size if reconfigured */
                if (dict_get_uint32 (options, "nfs.drc-size", &drc_size))
                        drc_size = DRC_DEFAULT_CACHE_SIZE;

                /* case 1: sub-case 1*/
                if (drc->global_cache_size == drc_size)
                        return (0);

                /* case 1: sub-case 2*/
                (void) rpcsvc_drc_deinit (svc);
                return rpcsvc_drc_init (svc, options);
        }

        /* case 2: DRC is "OFF" */
        return rpcsvc_drc_deinit (svc);
}
