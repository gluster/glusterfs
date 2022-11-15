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

#include "glusterfs/locking.h" /* for gf_lock_t, not included by glusterfs.h */
#include "glusterfs/atomic.h"  /* for gf_atomic_t */

/* auth_data structure is required by RPC layer. But as it is also used in
 * client_t structure validation, comparision, it is critical that it is defined
 * in the larger scope of libglusterfs, instead of libgfrpc. With this change,
 * even RPC will use this structure */
#define GF_CLIENTT_AUTH_BYTES 400
typedef struct client_auth_data {
    int flavour;
    int datalen;
    char authdata[GF_CLIENTT_AUTH_BYTES];
} client_auth_data_t;

struct client_ctx {
    void *ctx_key;
    void *ctx_value;
};

#define GF_CLIENTCTX_INITIAL_SIZE 8

typedef struct _client {
    gf_atomic_t bind;
    gf_atomic_t count;
    xlator_t *bound_xl;
    xlator_t *this;
    int tbl_index;
    int32_t opversion;
    char *client_name;
    struct {
        int flavour;
        size_t len;
        char *data;
        char *username;
        char *passwd;
    } auth;

    /* subdir_mount */
    char *subdir_mount;
    inode_t *subdir_inode;
    uuid_t subdir_gfid;
    /* Variable to save fd_count for detach brick */
    gf_atomic_t fd_cnt;
    gf_lock_t scratch_ctx_lock;
    struct client_ctx scratch_ctx[GF_CLIENTCTX_INITIAL_SIZE];
    char client_uid[];
} client_t;

struct client_table_entry {
    client_t *client;
    int next_free;
};
typedef struct client_table_entry cliententry_t;

struct clienttable {
    unsigned int max_clients;
    int first_free;
    gf_lock_t lock;
    cliententry_t *cliententries;
    client_t *local;
};
typedef struct clienttable clienttable_t;

#define GF_CLIENTTABLE_INITIAL_SIZE 128

/* Signifies no more entries in the client table. */
#define GF_CLIENTTABLE_END -1

/* This is used to invalidate
 * the next_free value in an cliententry that has been allocated
 */
#define GF_CLIENTENTRY_ALLOCATED -2

void
gf_client_put(client_t *client, gf_boolean_t *detached);

clienttable_t *
gf_clienttable_alloc(void);

client_t *
gf_client_ref(client_t *client);

void
gf_client_unref(client_t *client);

int
gf_client_dump_fdtable_to_dict(xlator_t *this, dict_t *dict);

int
gf_client_dump_fdtable(xlator_t *this);

int
gf_client_dump_inodes_to_dict(xlator_t *this, dict_t *dict);

int
gf_client_dump_inodes(xlator_t *this);

void *
client_ctx_set(client_t *client, void *key, void *value);

void *
client_ctx_get(client_t *client, void *key);

void *
client_ctx_del(client_t *client, void *key);

void
client_ctx_dump(client_t *client, char *prefix);

int
gf_client_dump_fdtables_to_dict(xlator_t *this, dict_t *dict);

int
gf_client_dump_fdtables(xlator_t *this);

int
gf_client_dump_inodes_to_dict(xlator_t *this, dict_t *dict);

int
gf_client_dump_inodes(xlator_t *this);

int
gf_client_disconnect(client_t *client);

client_t *
gf_client_get(xlator_t *this, client_auth_data_t *cred, char *client_uid,
              char *subdir_mount);

#endif /* _CLIENT_T_H */
