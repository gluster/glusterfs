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
#include "lock-table.h"
#include "rpcsvc.h"
#include "client_t.h"


#ifndef _CONFIG_H
#define _CONFIG_H
#include "config.h"
#endif


static int
gf_client_chain_client_entries (cliententry_t *entries, uint32_t startidx,
                        uint32_t endcount)
{
        uint32_t        i = 0;

        if (!entries) {
                gf_log_callingfn ("client_t", GF_LOG_WARNING, "!entries");
                return -1;
        }

        /* Chain only till the second to last entry because we want to
         * ensure that the last entry has GF_CLIENTTABLE_END.
         */
        for (i = startidx; i < (endcount - 1); i++)
                entries[i].next_free = i + 1;

        /* i has already been incremented upto the last entry. */
        entries[i].next_free = GF_CLIENTTABLE_END;

        return 0;
}


static int
gf_client_clienttable_expand (clienttable_t *clienttable, uint32_t nr)
{
        cliententry_t   *oldclients     = NULL;
        uint32_t         oldmax_clients = -1;
        int              ret            = -1;

        if (clienttable == NULL || nr < 0) {
                gf_log_callingfn ("client_t", GF_LOG_ERROR, "invalid argument");
                ret = EINVAL;
                goto out;
        }

        /* expand size by power-of-two...
           this originally came from .../xlators/protocol/server/src/server.c
           where it was not commented */
        nr /= (1024 / sizeof (cliententry_t));
        nr = gf_roundup_next_power_of_two (nr + 1);
        nr *= (1024 / sizeof (cliententry_t));

        oldclients = clienttable->cliententries;
        oldmax_clients = clienttable->max_clients;

        clienttable->cliententries = GF_CALLOC (nr, sizeof (cliententry_t),
                                                gf_common_mt_cliententry_t);
        if (!clienttable->cliententries) {
                ret = ENOMEM;
                goto out;
        }
        clienttable->max_clients = nr;

        if (oldclients) {
                uint32_t cpy = oldmax_clients * sizeof (cliententry_t);
                memcpy (clienttable->cliententries, oldclients, cpy);
        }

        gf_client_chain_client_entries (clienttable->cliententries, oldmax_clients,
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

        clienttable =
                GF_CALLOC (1, sizeof (*clienttable), gf_common_mt_clienttable_t);
        if (!clienttable)
                return NULL;

        LOCK_INIT (&clienttable->lock);
        gf_client_clienttable_expand (clienttable, GF_CLIENTTABLE_INITIAL_SIZE);
        return clienttable;
}


void
gf_client_clienttable_destroy (clienttable_t *clienttable)
{
        struct list_head  list          = {0, };
        client_t         *client        = NULL;
        cliententry_t    *cliententries = NULL;
        uint32_t          client_count  = 0;
        int32_t           i             = 0;

        INIT_LIST_HEAD (&list);

        if (!clienttable) {
                gf_log_callingfn ("client_t", GF_LOG_WARNING, "!clienttable");
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


client_t *
gf_client_get (xlator_t *this, rpcsvc_auth_data_t *cred, char *client_uid)
{
        client_t      *client      = NULL;
        cliententry_t *cliententry = NULL;
        clienttable_t *clienttable = NULL;
        unsigned int   i           = 0;

        if (this == NULL || client_uid == NULL) {
                gf_log_callingfn ("client_t", GF_LOG_ERROR, "invalid argument");
                errno = EINVAL;
                return NULL;
        }

        gf_log (this->name, GF_LOG_INFO, "client_uid=%s", client_uid);

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
                        if (strcmp (client_uid, client->server_ctx.client_uid) == 0 &&
                                (cred->flavour != AUTH_NONE &&
                                        (cred->flavour == client->server_ctx.auth.flavour &&
                                        (size_t) cred->datalen == client->server_ctx.auth.len &&
                                        memcmp (cred->authdata,
                                                client->server_ctx.auth.data,
                                                client->server_ctx.auth.len) == 0))) {
#if __GNUC__ > 4 || (__GNUC__ == 4 && __GNUC_MINOR__ >= 1)
                                __sync_add_and_fetch(&client->ref.bind, 1);
#else
                                LOCK (&client->ref.lock);
                                {
                                        ++client->ref.bind;
                                }
                                UNLOCK (&client->ref.lock);
#endif
                                break;
                        }
                }
                if (client) {
                        gf_client_ref (client);
                        goto unlock;
                }
                client = GF_CALLOC (1, sizeof(client_t), gf_common_mt_client_t);
                if (client == NULL) {
                        errno = ENOMEM;
                        goto unlock;
                }

                client->this = this;
             /* client->server_ctx.lk_version = 0; redundant */

                LOCK_INIT (&client->server_ctx.fdtable_lock);
                LOCK_INIT (&client->locks_ctx.ltable_lock);
                LOCK_INIT (&client->scratch_ctx.lock);
                LOCK_INIT (&client->ref.lock);

                client->server_ctx.client_uid = gf_strdup (client_uid);
                if (client->server_ctx.client_uid == NULL) {
                        errno = ENOMEM;
                        GF_FREE (client);
                        client = NULL;
                        goto unlock;
                }
                client->server_ctx.fdtable = gf_fd_fdtable_alloc ();
                if (client->server_ctx.fdtable == NULL) {
                        errno = ENOMEM;
                        GF_FREE (client->server_ctx.client_uid);
                        GF_FREE (client);
                        client = NULL;
                        goto unlock;
                }

                client->locks_ctx.ltable = gf_lock_table_new ();
                if (client->locks_ctx.ltable == NULL) {
                        errno = ENOMEM;
                        GF_FREE (client->server_ctx.fdtable);
                        GF_FREE (client->server_ctx.client_uid);
                        GF_FREE (client);
                        client = NULL;
                        goto unlock;
                }

                /* no need to do these atomically here */
                client->ref.bind = client->ref.count = 1;

                client->server_ctx.auth.flavour = cred->flavour;
                if (cred->flavour != AUTH_NONE) {
                        client->server_ctx.auth.data =
                                GF_CALLOC (1, cred->datalen, gf_common_mt_client_t);
                        if (client->server_ctx.auth.data == NULL) {
                                errno = ENOMEM;
                                GF_FREE (client->locks_ctx.ltable);
                                GF_FREE (client->server_ctx.fdtable);
                                GF_FREE (client->server_ctx.client_uid);
                                GF_FREE (client);
                                client = NULL;
                                goto unlock;
                        }
                        memcpy (client->server_ctx.auth.data, cred->authdata,
                                cred->datalen);
                        client->server_ctx.auth.len = cred->datalen;
                }

                client->tbl_index = clienttable->first_free;
                cliententry = &clienttable->cliententries[client->tbl_index];
                cliententry->client = client;
                clienttable->first_free = cliententry->next_free;
                cliententry->next_free = GF_CLIENTENTRY_ALLOCATED;
                gf_client_ref (client);
        }
unlock:
        UNLOCK (&clienttable->lock);

        return client;
}

void
gf_client_put (client_t *client, gf_boolean_t *detached)
{
        gf_boolean_t unref = _gf_false;
        int bind_ref;

        if (detached)
                *detached = _gf_false;

#if __GNUC__ > 4 || (__GNUC__ == 4 && __GNUC_MINOR__ >= 1)
        bind_ref = __sync_sub_and_fetch(&client->ref.bind, 1);
#else
        LOCK (&client->ref.lock);
        {
                bind_ref = --client->ref.bind;
        }
        UNLOCK (&client->ref.lock);
#endif
        if (bind_ref == 0)
                unref = _gf_true;

        if (unref) {
                gf_log (THIS->name, GF_LOG_INFO, "Shutting down connection %s",
                        client->server_ctx.client_uid);
                if (detached)
                        *detached = _gf_true;
                gf_client_unref (client);
        }
}


client_t *
gf_client_ref (client_t *client)
{
        if (!client) {
                gf_log_callingfn ("client_t", GF_LOG_ERROR, "null client");
                return NULL;
        }

#if __GNUC__ > 4 || (__GNUC__ == 4 && __GNUC_MINOR__ >= 1)
        __sync_add_and_fetch(&client->ref.count, 1);
#else
        LOCK (&client->ref.lock);
        {
                ++client->ref.count;
        }
        UNLOCK (&client->ref.lock);
#endif
        return client;
}


static void
client_destroy (client_t *client)
{
        clienttable_t *clienttable = NULL;

        if (client == NULL){
                gf_log_callingfn ("xlator", GF_LOG_ERROR, "invalid argument");
                goto out;
        }

        clienttable = client->this->ctx->clienttable;

        LOCK_DESTROY (&client->server_ctx.fdtable_lock);
        LOCK_DESTROY (&client->locks_ctx.ltable_lock);
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

        GF_FREE (client->server_ctx.auth.data);
        GF_FREE (client->scratch_ctx.ctx);
        GF_FREE (client->locks_ctx.ltable);
        GF_FREE (client->server_ctx.fdtable);
        GF_FREE (client->server_ctx.client_uid);
        GF_FREE (client);
out:
        return;
}


void
gf_client_unref (client_t *client)
{
        int refcount;

        if (!client) {
                gf_log_callingfn ("client_t", GF_LOG_ERROR, "client is NULL");
                return;
        }

#if __GNUC__ > 4 || (__GNUC__ == 4 && __GNUC_MINOR__ >= 1)
        refcount = __sync_sub_and_fetch(&client->ref.count, 1);
#else
        LOCK (&client->ref.lock);
        {
                refcount = --client->ref.count;
        }
        UNLOCK (&client->ref.lock);
#endif
        if (refcount == 0) {
                client_destroy (client);
        }
}


int
__client_ctx_set (client_t *client, xlator_t *xlator, uint64_t value)
{
        int index   = 0;
        int ret     = 0;
        int set_idx = -1;

        if (!client || !xlator)
                return -1;

        for (index = 0; index < client->scratch_ctx.count; index++) {
                if (!client->scratch_ctx.ctx[index].key) {
                        if (set_idx == -1)
                                set_idx = index;
                        /* dont break, to check if key already exists
                           further on */
                }
                if (client->scratch_ctx.ctx[index].xl_key == xlator) {
                        set_idx = index;
                        break;
                }
        }

        if (set_idx == -1) {
                gf_log_callingfn ("", GF_LOG_WARNING, "%p %s", client, xlator->name);
                ret = -1;
                goto out;
        }

        client->scratch_ctx.ctx[set_idx].xl_key = xlator;
        client->scratch_ctx.ctx[set_idx].value  = value;

out:
        return ret;
}


int
client_ctx_set (client_t *client, xlator_t *xlator, uint64_t value)
{
        int ret = 0;

        if (!client || !xlator) {
                gf_log_callingfn ("", GF_LOG_WARNING, "%p %p", client, xlator);
                return -1;
        }

        LOCK (&client->scratch_ctx.lock);
        {
                ret = __client_ctx_set (client, xlator, value);
        }
        UNLOCK (&client->scratch_ctx.lock);

        return ret;
}


int
__client_ctx_get (client_t *client, xlator_t *xlator, uint64_t *value)
{
        int index = 0;
        int ret   = 0;

        if (!client || !xlator)
                return -1;

        for (index = 0; index < client->scratch_ctx.count; index++) {
                if (client->scratch_ctx.ctx[index].xl_key == xlator)
                        break;
        }

        if (index == client->scratch_ctx.count) {
                ret = -1;
                goto out;
        }

        if (value)
                *value = client->scratch_ctx.ctx[index].value;

out:
        return ret;
}


int
client_ctx_get (client_t *client, xlator_t *xlator, uint64_t *value)
{
        int ret = 0;

        if (!client || !xlator)
                return -1;

        LOCK (&client->scratch_ctx.lock);
        {
                ret = __client_ctx_get (client, xlator, value);
        }
        UNLOCK (&client->scratch_ctx.lock);

        return ret;
}


int
__client_ctx_del (client_t *client, xlator_t *xlator, uint64_t *value)
{
        int index = 0;
        int ret   = 0;

        if (!client || !xlator)
                return -1;

        for (index = 0; index < client->scratch_ctx.count; index++) {
                if (client->scratch_ctx.ctx[index].xl_key == xlator)
                        break;
        }

        if (index == client->scratch_ctx.count) {
                ret = -1;
                goto out;
        }

        if (value)
                *value = client->scratch_ctx.ctx[index].value;

        client->scratch_ctx.ctx[index].key   = 0;
        client->scratch_ctx.ctx[index].value = 0;

out:
        return ret;
}


int
client_ctx_del (client_t *client, xlator_t *xlator, uint64_t *value)
{
        int ret = 0;

        if (!client || !xlator)
                return -1;

        LOCK (&client->scratch_ctx.lock);
        {
                ret = __client_ctx_del (client, xlator, value);
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
                        gf_log ("client_t", GF_LOG_WARNING,
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
        client_t       *client      = NULL;
        clienttable_t  *clienttable = NULL;
        int             count       = 0;
        int             ret         = -1;
        char            key[GF_DUMP_MAX_BUF_LEN] = {0,};

        GF_VALIDATE_OR_GOTO (THIS->name, this, out);
        GF_VALIDATE_OR_GOTO (this->name, dict, out);

        clienttable = this->ctx->clienttable;

        if (!clienttable)
                return -1;

        ret = TRY_LOCK (&clienttable->lock);
        {
                if (ret) {
                        gf_log ("client_t", GF_LOG_WARNING,
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
                        gf_log ("client_t", GF_LOG_WARNING,
                                "Unable to acquire lock");
                        return -1;
                }


                for ( ; count < clienttable->max_clients; count++) {
                        if (GF_CLIENTENTRY_ALLOCATED !=
                            clienttable->cliententries[count].next_free)
                                continue;
                        client = clienttable->cliententries[count].client;
                        memset(key, 0, sizeof key);
                        if (client->server_ctx.client_uid) {
                                gf_proc_dump_build_key (key, "conn",
                                                        "%d.id", count);
                                gf_proc_dump_write (key, "%s",
                                                    client->server_ctx.client_uid);
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

                        gf_proc_dump_build_key (key, "conn","%d.id", count);
                        fdtable_dump (client->server_ctx.fdtable, key);
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
                        gf_log ("client_t", GF_LOG_WARNING,
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
        int             count         = 1;
        int             ret           = -1;
        char            key[GF_DUMP_MAX_BUF_LEN] = {0,};

        GF_VALIDATE_OR_GOTO (THIS->name, this, out);

        clienttable = this->ctx->clienttable;

        if (!clienttable)
                return -1;

        ret = TRY_LOCK (&clienttable->lock);
        {
                if (ret) {
                        gf_log ("client_t", GF_LOG_WARNING,
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
