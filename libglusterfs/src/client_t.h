/*
  Copyright (c) 2008-2012 Red Hat, Inc. <http://www.redhat.com>
  This file is part of GlusterFS.

  This file is licensed to you under your choice of the GNU Lesser
  General Public License, version 3 or any later version (LGPLv3 or
  later), or the GNU General Public License, version 2 (GPLv2), in all
  cases as published by the Free Software Foundation.
*/

#ifndef _CLIENT_T_H
#define _CLIENT_T_H

#include "glusterfs.h"
#include "locking.h"  /* for gf_lock_t, not included by glusterfs.h */

struct client_ctx {
        void     *ctx_key;
        void     *ctx_value;
};

typedef struct _client_t {
        struct {
                /* e.g. protocol/server stashes its ctx here */
                gf_lock_t            lock;
                unsigned short       count;
                struct client_ctx   *ctx;
        }            scratch_ctx;
        struct {
                gf_lock_t            lock;
                volatile int         bind;
                volatile int         count;
        }            ref;
        xlator_t    *bound_xl;
        xlator_t    *this;
        int          tbl_index;
        char        *client_uid;
        struct {
                int                  flavour;
                size_t               len;
                char                *data;
                char                *username;
                char                *passwd;
        }            auth;
} client_t;

#define GF_CLIENTCTX_INITIAL_SIZE 8

struct client_table_entry {
        client_t            *client;
        int                  next_free;
};
typedef struct client_table_entry cliententry_t;

struct clienttable {
        unsigned int         max_clients;
        gf_lock_t            lock;
        cliententry_t       *cliententries;
        int                  first_free;
	client_t            *local;
};
typedef struct clienttable clienttable_t;

#define GF_CLIENTTABLE_INITIAL_SIZE 128

/* Signifies no more entries in the client table. */
#define GF_CLIENTTABLE_END  -1

/* This is used to invalidate
 * the next_free value in an cliententry that has been allocated
 */
#define GF_CLIENTENTRY_ALLOCATED    -2

struct rpcsvc_auth_data;

client_t *
gf_client_get (xlator_t *this, struct rpcsvc_auth_data *cred, char *client_uid);

void
gf_client_put (client_t *client, gf_boolean_t *detached);

clienttable_t *
gf_clienttable_alloc (void);

void
gf_client_clienttable_destroy (clienttable_t *clienttable);

client_t *
gf_client_ref (client_t *client);

void
gf_client_unref (client_t *client);

int
gf_client_dump_fdtable_to_dict (xlator_t *this, dict_t *dict);

int
gf_client_dump_fdtable (xlator_t *this);

int
gf_client_dump_inodes_to_dict (xlator_t *this, dict_t *dict);

int
gf_client_dump_inodes (xlator_t *this);

int
client_ctx_set (client_t *client, void *key, void *value);

int
client_ctx_get (client_t *client, void *key, void **value);

int
client_ctx_del (client_t *client, void *key, void **value);

void
client_ctx_dump (client_t *client, char *prefix);

int
gf_client_dump_fdtables_to_dict (xlator_t *this, dict_t *dict);

int
gf_client_dump_fdtables (xlator_t *this);

int
gf_client_dump_inodes_to_dict (xlator_t *this, dict_t *dict);

int
gf_client_dump_inodes (xlator_t *this);

int
gf_client_disconnect (client_t *client);

#endif /* _CLIENT_T_H */
