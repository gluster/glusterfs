/*
  Copyright (c) 2008-2013 Red Hat, Inc. <http://www.redhat.com>
  This file is part of GlusterFS.

  This file is licensed to you under your choice of the GNU Lesser
  General Public License, version 3 or any later version (LGPLv3 or
  later), or the GNU General Public License, version 2 (GPLv2), in all
  cases as published by the Free Software Foundation.
*/

#include "glusterfs.h"
#include "dict.h"
#include "statedump.h"
#include "client_t.h"
#include "list.h"
#include "rpcsvc.h"
#include "libglusterfs-messages.h"

static int
gf_client_chain_client_entries (cliententry_t *entries, uint32_t startidx,
                        uint32_t endcount)
{
        uint32_t        i = 0;

        if (!entries) {
                gf_msg_callingfn ("client_t", GF_LOG_WARNING, EINVAL,
                                  LG_MSG_INVALID_ARG, "!entries");
                return -1;
        }

        /* Chain only till the second to last entry because we want to
         * ensure that the last entry has GF_CLIENTTABLE_END.
         */
        for (i = startidx; i < (endcount - 1); i++)
                entries[i].next_free = i + 1;

        /* i has already been incremented up to the last entry. */
        entries[i].next_free = GF_CLIENTTABLE_END;

        return 0;
}


static int
gf_client_clienttable_expand (clienttable_t *clienttable, uint32_t nr)
{
        cliententry_t   *oldclients     = NULL;
        uint32_t         oldmax_clients = -1;
        int              ret            = -1;

        if (clienttable == NULL || nr <= clienttable->max_clients) {
                gf_msg_callingfn ("client_t", GF_LOG_ERROR, EINVAL,
                                  LG_MSG_INVALID_ARG, "invalid argument");
                ret = EINVAL;
                goto out;
        }

        oldclients = clienttable->cliententries;
        oldmax_clients = clienttable->max_clients;

        clienttable->cliententries = GF_CALLOC (nr, sizeof (cliententry_t),
                                                gf_common_mt_cliententry_t);
        if (!clienttable->cliententries) {
                clienttable->cliententries = oldclients;
                ret = 0;
                goto out;
        }
        clienttable->max_clients = nr;

        if (oldclients) {
                uint32_t cpy = oldmax_clients * sizeof (cliententry_t);
                memcpy (clienttable->cliententries, oldclients, cpy);
        }

        gf_client_chain_client_entries (clienttable->cliententries,
                                        oldmax_clients,
                                        clienttable->max_clients);

        /* Now that expansion is done, we must update the client list
         * head pointer so that the client allocation functions can continue
         * using the expanded table.
         */
        clienttable->first_free = oldmax_clients;
        GF_FREE (oldclients);
        ret = 0;
out:
        return ret;
}


clienttable_t *
gf_clienttable_alloc (void)
{
        clienttable_t *clienttable = NULL;
        int            result = 0;

        clienttable =
                GF_CALLOC (1, sizeof (clienttable_t), gf_common_mt_clienttable_t);
        if (!clienttable)
                return NULL;

        LOCK_INIT (&clienttable->lock);

        result = gf_client_clienttable_expand (clienttable,
                                               GF_CLIENTTABLE_INITIAL_SIZE);
        if (result != 0) {
                gf_msg ("client_t", GF_LOG_ERROR, 0,
                        LG_MSG_EXPAND_CLIENT_TABLE_FAILED,
                        "gf_client_clienttable_expand failed");
                GF_FREE (clienttable);
                return NULL;
        }

        return clienttable;
}


void
gf_client_clienttable_destroy (clienttable_t *clienttable)
{
        client_t         *client        = NULL;
        cliententry_t    *cliententries = NULL;
        uint32_t          client_count  = 0;
        int32_t           i             = 0;

        if (!clienttable) {
                gf_msg_callingfn ("client_t", GF_LOG_WARNING, EINVAL,
                                  LG_MSG_INVALID_ARG, "!clienttable");
                return;
        }

        LOCK (&clienttable->lock);
        {
                client_count = clienttable->max_clients;
                clienttable->max_clients = 0;
                cliententries = clienttable->cliententries;
                clienttable->cliententries = NULL;
        }
        UNLOCK (&clienttable->lock);

        if (cliententries != NULL) {
                for (i = 0; i < client_count; i++) {
                        client = cliententries[i].client;
                        if (client != NULL) {
                                gf_client_unref (client);
                        }
                }

                GF_FREE (cliententries);
                LOCK_DESTROY (&clienttable->lock);
                GF_FREE (clienttable);
        }
}


/*
 * Increments ref.bind if the client is already present or creates a new
 * client with ref.bind = 1,ref.count = 1 it signifies that
 * as long as ref.bind is > 0 client should be alive.
 */
client_t *
gf_client_get (xlator_t *this, struct rpcsvc_auth_data *cred, char *client_uid)
{
        client_t      *client      = NULL;
        cliententry_t *cliententry = NULL;
        clienttable_t *clienttable = NULL;
        unsigned int   i           = 0;

        if (this == NULL || client_uid == NULL) {
                gf_msg_callingfn ("client_t", GF_LOG_ERROR, EINVAL,
                                  LG_MSG_INVALID_ARG, "invalid argument");
                errno = EINVAL;
                return NULL;
        }

        clienttable = this->ctx->clienttable;

        LOCK (&clienttable->lock);
        {
                for (; i < clienttable->max_clients; i++) {
                        client = clienttable->cliententries[i].client;
                        if (client == NULL)
                                continue;
                        /*
                         * look for matching client_uid, _and_
                         * if auth was used, matching auth flavour and data
                         */
                        if (strcmp (client_uid, client->client_uid) == 0 &&
                                (cred->flavour != AUTH_NONE &&
                                        (cred->flavour == client->auth.flavour &&
                                        (size_t) cred->datalen == client->auth.len &&
                                        memcmp (cred->authdata,
                                                client->auth.data,
                                                client->auth.len) == 0))) {
                                INCREMENT_ATOMIC (client->ref.lock,
                                                  client->ref.bind);
                                goto unlock;
                        }
                }

                client = GF_CALLOC (1, sizeof(client_t), gf_common_mt_client_t);
                if (client == NULL) {
                        errno = ENOMEM;
                        goto unlock;
                }

                client->this = this;

                LOCK_INIT (&client->scratch_ctx.lock);
                LOCK_INIT (&client->ref.lock);

                client->client_uid = gf_strdup (client_uid);
                if (client->client_uid == NULL) {
                        GF_FREE (client);
                        client = NULL;
                        errno = ENOMEM;
                        goto unlock;
                }
                client->scratch_ctx.count = GF_CLIENTCTX_INITIAL_SIZE;
                client->scratch_ctx.ctx =
                        GF_CALLOC (GF_CLIENTCTX_INITIAL_SIZE,
                                   sizeof (struct client_ctx),
                                   gf_common_mt_client_ctx);
                if (client->scratch_ctx.ctx == NULL) {
                        GF_FREE (client->client_uid);
                        GF_FREE (client);
                        client = NULL;
                        errno = ENOMEM;
                        goto unlock;
                }

                /* no need to do these atomically here */
                client->ref.bind = client->ref.count = 1;

                client->auth.flavour = cred->flavour;
                if (cred->flavour != AUTH_NONE) {
                        client->auth.data =
                                GF_CALLOC (1, cred->datalen,
                                           gf_common_mt_client_t);
                        if (client->auth.data == NULL) {
                                GF_FREE (client->scratch_ctx.ctx);
                                GF_FREE (client->client_uid);
                                GF_FREE (client);
                                client = NULL;
                                errno = ENOMEM;
                                goto unlock;
                        }
                        memcpy (client->auth.data, cred->authdata,
                                cred->datalen);
                        client->auth.len = cred->datalen;
                }

                client->tbl_index = clienttable->first_free;
                cliententry = &clienttable->cliententries[clienttable->first_free];
                if (cliententry->next_free == GF_CLIENTTABLE_END) {
                        int result =
                                gf_client_clienttable_expand (clienttable,
                                        clienttable->max_clients +
                                                GF_CLIENTTABLE_INITIAL_SIZE);
                        if (result != 0) {
                                GF_FREE (client->scratch_ctx.ctx);
                                GF_FREE (client->client_uid);
                                GF_FREE (client);
                                client = NULL;
                                errno = result;
                                goto unlock;
                        }
                        cliententry = &clienttable->cliententries[client->tbl_index];
                        cliententry->next_free = clienttable->first_free;
                }
                cliententry->client = client;
                clienttable->first_free = cliententry->next_free;
                cliententry->next_free = GF_CLIENTENTRY_ALLOCATED;
        }
unlock:
        UNLOCK (&clienttable->lock);

        if (client)
                gf_msg_callingfn ("client_t", GF_LOG_DEBUG, 0, LG_MSG_BIND_REF,
                                  "%s: bind_ref: %d, ref: %d",
                                  client->client_uid, client->ref.bind,
                                  client->ref.count);
        return client;
}

void
gf_client_put (client_t *client, gf_boolean_t *detached)
{
        gf_boolean_t unref = _gf_false;
        int bind_ref;

        if (client == NULL)
                goto out;

        if (detached)
                *detached = _gf_false;

        bind_ref = DECREMENT_ATOMIC (client->ref.lock, client->ref.bind);
        if (bind_ref == 0)
                unref = _gf_true;

        gf_msg_callingfn ("client_t", GF_LOG_DEBUG, 0, LG_MSG_BIND_REF, "%s: "
                          "bind_ref: %d, ref: %d, unref: %d",
                          client->client_uid, client->ref.bind,
                          client->ref.count, unref);
        if (unref) {
                if (detached)
                        *detached = _gf_true;
                gf_client_unref (client);
        }

out:
        return;
}

client_t *
gf_client_ref (client_t *client)
{
        if (!client) {
                gf_msg_callingfn ("client_t", GF_LOG_ERROR, EINVAL,
                                  LG_MSG_INVALID_ARG, "null client");
                return NULL;
        }

        INCREMENT_ATOMIC (client->ref.lock, client->ref.count);
        gf_msg_callingfn ("client_t", GF_LOG_DEBUG, 0, LG_MSG_REF_COUNT, "%s: "
                          "ref-count %d", client->client_uid,
                          client->ref.count);
        return client;
}


static void
gf_client_destroy_recursive (xlator_t *xl, client_t *client)
{
        xlator_list_t   *trav;

        if (xl->cbks->client_destroy) {
                xl->cbks->client_destroy (xl, client);
        }

        for (trav = xl->children; trav; trav = trav->next) {
                gf_client_destroy_recursive (trav->xlator, client);
        }
}


static void
client_destroy (client_t *client)
{
        clienttable_t     *clienttable = NULL;
        glusterfs_graph_t *gtrav       = NULL;

        if (client == NULL){
                gf_msg_callingfn ("xlator", GF_LOG_ERROR, EINVAL,
                                  LG_MSG_INVALID_ARG, "invalid argument");
                goto out;
        }

        clienttable = client->this->ctx->clienttable;

        LOCK_DESTROY (&client->scratch_ctx.lock);
        LOCK_DESTROY (&client->ref.lock);

        LOCK (&clienttable->lock);
        {
                clienttable->cliententries[client->tbl_index].client = NULL;
                clienttable->cliententries[client->tbl_index].next_free =
                        clienttable->first_free;
                clienttable->first_free = client->tbl_index;
        }
        UNLOCK (&clienttable->lock);

        list_for_each_entry (gtrav, &client->this->ctx->graphs, list) {
                gf_client_destroy_recursive (gtrav->top, client);
        }
        GF_FREE (client->auth.data);
        GF_FREE (client->auth.username);
        GF_FREE (client->auth.passwd);
        GF_FREE (client->scratch_ctx.ctx);
        GF_FREE (client->client_uid);
        GF_FREE (client);
out:
        return;
}

static int
gf_client_disconnect_recursive (xlator_t *xl, client_t *client)
{
        int             ret     = 0;
        xlator_list_t   *trav;

        if (xl->cbks->client_disconnect) {
                ret = xl->cbks->client_disconnect (xl, client);
        }

        for (trav = xl->children; trav; trav = trav->next) {
                ret |= gf_client_disconnect_recursive (trav->xlator, client);
        }

        return ret;
}


int
gf_client_disconnect (client_t *client)
{
        int                ret   = 0;
        glusterfs_graph_t *gtrav = NULL;

        list_for_each_entry (gtrav, &client->this->ctx->graphs, list) {
                ret |= gf_client_disconnect_recursive (gtrav->top, client);
        }

        return ret;
}


void
gf_client_unref (client_t *client)
{
        int refcount;

        if (!client) {
                gf_msg_callingfn ("client_t", GF_LOG_ERROR, EINVAL,
                                  LG_MSG_INVALID_ARG, "client is NULL");
                return;
        }

        refcount = DECREMENT_ATOMIC (client->ref.lock, client->ref.count);
        gf_msg_callingfn ("client_t", GF_LOG_DEBUG, 0, LG_MSG_REF_COUNT, "%s: "
                          "ref-count %d", client->client_uid,
                          (int)client->ref.count);
        if (refcount == 0) {
                gf_msg (THIS->name, GF_LOG_INFO, 0, LG_MSG_DISCONNECT_CLIENT,
                        "Shutting down connection %s", client->client_uid);
                client_destroy (client);
        }
}


static int
client_ctx_set_int (client_t *client, void *key, void *value)
{
        int index   = 0;
        int ret     = 0;
        int set_idx = -1;

        for (index = 0; index < client->scratch_ctx.count; index++) {
                if (!client->scratch_ctx.ctx[index].ctx_key) {
                        if (set_idx == -1)
                                set_idx = index;
                        /* dont break, to check if key already exists
                           further on */
                }
                if (client->scratch_ctx.ctx[index].ctx_key == key) {
                        set_idx = index;
                        break;
                }
        }

        if (set_idx == -1) {
                ret = -1;
                goto out;
        }

        client->scratch_ctx.ctx[set_idx].ctx_key = key;
        client->scratch_ctx.ctx[set_idx].ctx_value  = value;

out:
        return ret;
}


int
client_ctx_set (client_t *client, void *key, void *value)
{
        int ret = 0;

        if (!client || !key)
                return -1;

        LOCK (&client->scratch_ctx.lock);
        {
                ret = client_ctx_set_int (client, key, value);
        }
        UNLOCK (&client->scratch_ctx.lock);

        return ret;
}


static int
client_ctx_get_int (client_t *client, void *key, void **value)
{
        int index = 0;
        int ret   = 0;

        for (index = 0; index < client->scratch_ctx.count; index++) {
                if (client->scratch_ctx.ctx[index].ctx_key == key)
                        break;
        }

        if (index == client->scratch_ctx.count) {
                ret = -1;
                goto out;
        }

        if (value)
                *value = client->scratch_ctx.ctx[index].ctx_value;

out:
        return ret;
}


int
client_ctx_get (client_t *client, void *key, void **value)
{
        int ret = 0;

        if (!client || !key)
                return -1;

        LOCK (&client->scratch_ctx.lock);
        {
                ret = client_ctx_get_int (client, key, value);
        }
        UNLOCK (&client->scratch_ctx.lock);

        return ret;
}


static int
client_ctx_del_int (client_t *client, void *key, void **value)
{
        int index = 0;
        int ret   = 0;

        for (index = 0; index < client->scratch_ctx.count; index++) {
                if (client->scratch_ctx.ctx[index].ctx_key == key)
                        break;
        }

        if (index == client->scratch_ctx.count) {
                ret = -1;
                goto out;
        }

        if (value)
                *value = client->scratch_ctx.ctx[index].ctx_value;

        client->scratch_ctx.ctx[index].ctx_key   = 0;
        client->scratch_ctx.ctx[index].ctx_value = 0;

out:
        return ret;
}


int
client_ctx_del (client_t *client, void *key, void **value)
{
        int ret = 0;

        if (!client || !key)
                return -1;

        LOCK (&client->scratch_ctx.lock);
        {
                ret = client_ctx_del_int (client, key, value);
        }
        UNLOCK (&client->scratch_ctx.lock);

        return ret;
}


void
client_dump (client_t *client, char *prefix)
{
        char key[GF_DUMP_MAX_BUF_LEN];

        if (!client)
                return;

        memset(key, 0, sizeof key);
        gf_proc_dump_write("refcount", "%d", client->ref.count);
}


void
cliententry_dump (cliententry_t *cliententry, char *prefix)
{
        if (!cliententry)
                return;

        if (GF_CLIENTENTRY_ALLOCATED != cliententry->next_free)
                return;

        if (cliententry->client)
                client_dump(cliententry->client, prefix);
}


void
clienttable_dump (clienttable_t *clienttable, char *prefix)
{
        int     i   = 0;
        int     ret = -1;
        char    key[GF_DUMP_MAX_BUF_LEN] = {0};

        if (!clienttable)
                return;

        ret = TRY_LOCK (&clienttable->lock);
        {
                if (ret) {
                        gf_msg ("client_t", GF_LOG_WARNING, 0,
                                LG_MSG_LOCK_FAILED,
                                "Unable to acquire lock");
                        return;
                }
                memset(key, 0, sizeof key);
                gf_proc_dump_build_key(key, prefix, "maxclients");
                gf_proc_dump_write(key, "%d", clienttable->max_clients);
                gf_proc_dump_build_key(key, prefix, "first_free");
                gf_proc_dump_write(key, "%d", clienttable->first_free);
                for ( i = 0 ; i < clienttable->max_clients; i++) {
                        if (GF_CLIENTENTRY_ALLOCATED ==
                            clienttable->cliententries[i].next_free) {
                                gf_proc_dump_build_key(key, prefix,
                                                       "cliententry[%d]", i);
                                gf_proc_dump_add_section(key);
                                cliententry_dump(&clienttable->cliententries[i],
                                                 key);
                        }
                }
        }
        UNLOCK(&clienttable->lock);
}


void
client_ctx_dump (client_t *client, char *prefix)
{
#if 0 /* TBD, FIXME */
        struct client_ctx   *client_ctx = NULL;
        xlator_t            *xl = NULL;
        int                  i = 0;

        if ((client == NULL) || (client->ctx == NULL)) {
                goto out;
        }

        LOCK (&client->ctx_lock);
        if (client->ctx != NULL) {
                client_ctx = GF_CALLOC (client->inode->table->xl->graph->ctx_count,
                                        sizeof (*client_ctx),
                                        gf_common_mt_client_ctx);
                if (client_ctx == NULL) {
                        goto unlock;
                }

                for (i = 0; i < client->inode->table->xl->graph->ctx_count; i++) {
                        client_ctx[i] = client->ctx[i];
                }
        }
unlock:
        UNLOCK (&client->ctx_lock);

        if (client_ctx == NULL) {
                goto out;
        }

        for (i = 0; i < client->inode->table->xl->graph->ctx_count; i++) {
                if (client_ctx[i].xl_key) {
                        xl = (xlator_t *)(long)client_ctx[i].xl_key;
                        if (xl->dumpops && xl->dumpops->clientctx)
                                xl->dumpops->clientctx (xl, client);
                }
        }
out:
        GF_FREE (client_ctx);
#endif
}


/*
 * the following functions are here to preserve legacy behavior of the
 * protocol/server xlator dump, but perhaps they should just be folded
 * into the client dump instead?
 */
int
gf_client_dump_fdtables_to_dict (xlator_t *this, dict_t *dict)
{
        clienttable_t  *clienttable = NULL;
        int             count       = 0;
        int             ret         = -1;
#ifdef NOTYET
        client_t       *client      = NULL;
        char            key[GF_DUMP_MAX_BUF_LEN] = {0,};
#endif

        GF_VALIDATE_OR_GOTO (THIS->name, this, out);
        GF_VALIDATE_OR_GOTO (this->name, dict, out);

        clienttable = this->ctx->clienttable;

        if (!clienttable)
                return -1;

#ifdef NOTYET
        ret = TRY_LOCK (&clienttable->lock);
        {
                if (ret) {
                        gf_msg ("client_t", GF_LOG_WARNING, 0,
                                LG_MSG_LOCK_FAILED,
                                "Unable to acquire lock");
                        return -1;
                }
                for ( ; count < clienttable->max_clients; count++) {
                        if (GF_CLIENTENTRY_ALLOCATED !=
                            clienttable->cliententries[count].next_free)
                                continue;
                        client = clienttable->cliententries[count].client;
                        memset(key, 0, sizeof key);
                        snprintf (key, sizeof key, "conn%d", count++);
                        fdtable_dump_to_dict (client->server_ctx.fdtable,
                                              key, dict);
                }
        }
        UNLOCK(&clienttable->lock);
#endif

        ret = dict_set_int32 (dict, "conncount", count);
out:
        return ret;
}

int
gf_client_dump_fdtables (xlator_t *this)
{
        client_t       *client = NULL;
        clienttable_t  *clienttable = NULL;
        int             count = 1;
        int             ret = -1;
        char            key[GF_DUMP_MAX_BUF_LEN] = {0,};

        GF_VALIDATE_OR_GOTO (THIS->name, this, out);

        clienttable = this->ctx->clienttable;

        if (!clienttable)
                return -1;

        ret = TRY_LOCK (&clienttable->lock);
        {
                if (ret) {
                        gf_msg ("client_t", GF_LOG_WARNING, 0,
                                LG_MSG_LOCK_FAILED,
                                "Unable to acquire lock");
                        return -1;
                }


                for ( ; count < clienttable->max_clients; count++) {
                        if (GF_CLIENTENTRY_ALLOCATED !=
                            clienttable->cliententries[count].next_free)
                                continue;
                        client = clienttable->cliententries[count].client;
                        memset(key, 0, sizeof key);
                        if (client->client_uid) {
                                gf_proc_dump_build_key (key, "conn",
                                                        "%d.id", count);
                                gf_proc_dump_write (key, "%s",
                                                    client->client_uid);
                        }

                        gf_proc_dump_build_key (key, "conn", "%d.ref",
                                                        count);
                        gf_proc_dump_write (key, "%d", client->ref.count);
                        if (client->bound_xl) {
                                gf_proc_dump_build_key (key, "conn",
                                                        "%d.bound_xl", count);
                                gf_proc_dump_write (key, "%s",
                                                    client->bound_xl->name);
                        }

#ifdef NOTYET
                        gf_proc_dump_build_key (key, "conn","%d.id", count);
                        fdtable_dump (client->server_ctx.fdtable, key);
#endif
                }
        }

        UNLOCK(&clienttable->lock);

        ret = 0;
out:
        return ret;
}


int
gf_client_dump_inodes_to_dict (xlator_t *this, dict_t *dict)
{
        client_t       *client        = NULL;
        clienttable_t  *clienttable   = NULL;
        xlator_t       *prev_bound_xl = NULL;
        char            key[32]       = {0,};
        int             count         = 0;
        int             ret           = -1;

        GF_VALIDATE_OR_GOTO (THIS->name, this, out);
        GF_VALIDATE_OR_GOTO (this->name, dict, out);

        clienttable = this->ctx->clienttable;

        if (!clienttable)
                return -1;

        ret = TRY_LOCK (&clienttable->lock);
        {
                if (ret) {
                        gf_msg ("client_t", GF_LOG_WARNING, 0,
                                LG_MSG_LOCK_FAILED,
                                "Unable to acquire lock");
                        return -1;
                }
                for ( ; count < clienttable->max_clients; count++) {
                        if (GF_CLIENTENTRY_ALLOCATED !=
                            clienttable->cliententries[count].next_free)
                                continue;
                        client = clienttable->cliententries[count].client;
                        memset(key, 0, sizeof key);
                        if (client->bound_xl && client->bound_xl->itable) {
                                /* Presently every brick contains only
                                 * one bound_xl for all connections.
                                 * This will lead to duplicating of
                                 *  the inode lists, if listing is
                                 * done for every connection. This
                                 * simple check prevents duplication
                                 * in the present case. If need arises
                                 * the check can be improved.
                                 */
                                if (client->bound_xl == prev_bound_xl)
                                        continue;
                                prev_bound_xl = client->bound_xl;

                                memset (key, 0, sizeof (key));
                                snprintf (key, sizeof (key), "conn%d", count);
                                inode_table_dump_to_dict (client->bound_xl->itable,
                                                          key, dict);
                        }
                }
        }
        UNLOCK(&clienttable->lock);

        ret = dict_set_int32 (dict, "conncount", count);

out:
        if (prev_bound_xl)
                prev_bound_xl = NULL;
        return ret;
}

int
gf_client_dump_inodes (xlator_t *this)
{
        client_t       *client        = NULL;
        clienttable_t  *clienttable   = NULL;
        xlator_t       *prev_bound_xl = NULL;
        int             count         = 0;
        int             ret           = -1;
        char            key[GF_DUMP_MAX_BUF_LEN] = {0,};

        GF_VALIDATE_OR_GOTO (THIS->name, this, out);

        clienttable = this->ctx->clienttable;

        if (!clienttable)
                goto out;

        ret = TRY_LOCK (&clienttable->lock);
        {
                if (ret) {
                        gf_msg ("client_t", GF_LOG_WARNING, 0,
                                LG_MSG_LOCK_FAILED,
                                "Unable to acquire lock");
                        goto out;
                }

                for ( ; count < clienttable->max_clients; count++) {
                        if (GF_CLIENTENTRY_ALLOCATED !=
                            clienttable->cliententries[count].next_free)
                                continue;
                        client = clienttable->cliententries[count].client;
                        memset(key, 0, sizeof key);
                        if (client->bound_xl && client->bound_xl->itable) {
                                /* Presently every brick contains only
                                 * one bound_xl for all connections.
                                 * This will lead to duplicating of
                                 * the inode lists, if listing is
                                 * done for every connection. This
                                 * simple check prevents duplication
                                 * in the present case. If need arises
                                 * the check can be improved.
                                 */
                                if (client->bound_xl == prev_bound_xl)
                                        continue;
                                prev_bound_xl = client->bound_xl;

                                gf_proc_dump_build_key(key, "conn",
                                                       "%d.bound_xl.%s", count,
                                                       client->bound_xl->name);
                                inode_table_dump(client->bound_xl->itable,key);
                        }
                }
        }
        UNLOCK(&clienttable->lock);

        ret = 0;
out:
        return ret;
}

