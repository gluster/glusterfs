/*
   Copyright (c) 2013 Red Hat, Inc. <http://www.redhat.com>
   This file is part of GlusterFS.

   This file is licensed to you under your choice of the GNU Lesser
   General Public License, version 3 or any later version (LGPLv3 or
   later), or the GNU General Public License, version 2 (GPLv2), in all
   cases as published by the Free Software Foundation.
*/
#ifndef _GLUSTERD_STORE_H_
#define _GLUSTERD_STORE_H_

#include "compat.h"
#include "glusterfs.h"

struct gf_store_handle_ {
        char    *path;
        int     fd;
        int     tmp_fd;
        FILE    *read;
        int     locked;   /* state of lockf() */
};

typedef struct gf_store_handle_ gf_store_handle_t;

struct gf_store_iter_ {
        FILE    *file;
        char    filepath[PATH_MAX];
};

typedef struct gf_store_iter_ gf_store_iter_t;

typedef enum {
        GD_STORE_SUCCESS,
        GD_STORE_KEY_NULL,
        GD_STORE_VALUE_NULL,
        GD_STORE_KEY_VALUE_NULL,
        GD_STORE_EOF,
        GD_STORE_ENOMEM,
        GD_STORE_STAT_FAILED
} gf_store_op_errno_t;

int32_t
gf_store_mkdir (char *path);

int32_t
gf_store_handle_create_on_absence (gf_store_handle_t **shandle, char *path);

int32_t
gf_store_mkstemp (gf_store_handle_t *shandle);

int
gf_store_sync_direntry (char *path);

int32_t
gf_store_rename_tmppath (gf_store_handle_t *shandle);

int32_t
gf_store_unlink_tmppath (gf_store_handle_t *shandle);

int
gf_store_read_and_tokenize (FILE *file, char *str, int size, char **iter_key,
                            char **iter_val, gf_store_op_errno_t *store_errno);

int32_t
gf_store_retrieve_value (gf_store_handle_t *handle, char *key, char **value);

int32_t
gf_store_save_value (int fd, char *key, char *value);

int32_t
gf_store_handle_new (const char *path, gf_store_handle_t **handle);

int
gf_store_handle_retrieve (char *path, gf_store_handle_t **handle);

int32_t
gf_store_handle_destroy (gf_store_handle_t *handle);

int32_t
gf_store_iter_new (gf_store_handle_t  *shandle, gf_store_iter_t  **iter);

int32_t
gf_store_validate_key_value (char *storepath, char *key, char *val,
                             gf_store_op_errno_t *op_errno);

int32_t
gf_store_iter_get_next (gf_store_iter_t *iter, char **key, char **value,
                        gf_store_op_errno_t *op_errno);

int32_t
gf_store_iter_get_matching (gf_store_iter_t *iter, char *key, char **value);

int32_t
gf_store_iter_destroy (gf_store_iter_t *iter);

char*
gf_store_strerror (gf_store_op_errno_t op_errno);

int
gf_store_lock (gf_store_handle_t *sh);

void
gf_store_unlock (gf_store_handle_t *sh);

int
gf_store_locked_local (gf_store_handle_t *sh);

#endif
