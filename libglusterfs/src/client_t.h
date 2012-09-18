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

#ifndef _CONFIG_H
#define _CONFIG_H
#include "config.h"
#endif

#include "glusterfs.h"
#include "locking.h"  /* for gf_lock_t, not included by glusterfs.h */

struct client_ctx {
        union {
                uint64_t  key;
                void     *xl_key;
        };
        union {
                uint64_t  value;
                void     *ptr1;
        };
};

struct _client_t {
        struct {
                /* ctx for .../xlators/protocol/server */
                gf_lock_t            fdtable_lock;
                fdtable_t           *fdtable;
                char                *client_uid;
                struct _gf_timer    *grace_timer;
                uint32_t             lk_version;
                struct {
                        int          flavour;
                        size_t       len;
                        char        *data;
                }            auth;
        }            server_ctx;
        struct {
                /* ctx for .../xlators/features/locks */
                gf_lock_t            ltable_lock;
                struct _lock_table  *ltable;
        }            locks_ctx;
        struct {
                /* e.g. hekafs uidmap can stash stuff here */
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
};
typedef struct _client_t client_t;


struct client_table_entry {
        client_t            *client;
        int                  next_free;
};
typedef struct client_table_entry cliententry_t;


struct _clienttable {
        unsigned int         max_clients;
        gf_lock_t            lock;
        cliententry_t       *cliententries;
        int                  first_free;
};
typedef struct _clienttable clienttable_t;

#define GF_CLIENTTABLE_INITIAL_SIZE 32

/* Signifies no more entries in the client table. */
#define GF_CLIENTTABLE_END  -1

/* This is used to invalidate
 * the next_free value in an cliententry that has been allocated
 */
#define GF_CLIENTENTRY_ALLOCATED    -2



client_t *
gf_client_get (xlator_t *this, rpcsvc_auth_data_t *cred, char *client_uid);

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
client_ctx_set (client_t *client, xlator_t *xlator, uint64_t value);


int
client_ctx_get (client_t *client, xlator_t *xlator, uint64_t *value);


int
client_ctx_del (client_t *client, xlator_t *xlator, uint64_t *value);


int
_client_ctx_set (client_t *client, xlator_t *xlator, uint64_t value);


int
_client_ctx_get (client_t *client, xlator_t *xlator, uint64_t *value);


int
_client_ctx_del (client_t *client, xlator_t *xlator, uint64_t *value);

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

#endif /* _CLIENT_T_H */
