/*
   Copyright (c) 2006-2012 Red Hat, Inc. <http://www.redhat.com>
   This file is part of GlusterFS.

   This file is licensed to you under your choice of the GNU Lesser
   General Public License, version 3 or any later version (LGPLv3 or
   later), or the GNU General Public License, version 2 (GPLv2), in all
   cases as published by the Free Software Foundation.
*/
#ifndef _GLUSTERD_HOOKS_H_
#define _GLUSTERD_HOOKS_H_

#include <fnmatch.h>

#define GLUSTERD_GET_HOOKS_DIR(path, version, priv) \
        snprintf (path, PATH_MAX, "%s/hooks/%d", priv->workdir,\
                  version);

#define GLUSTERD_HOOK_VER       1

#define GD_HOOKS_SPECIFIC_KEY   "user.*"

typedef enum glusterd_commit_hook_type {
        GD_COMMIT_HOOK_NONE = 0,
        GD_COMMIT_HOOK_PRE,
        GD_COMMIT_HOOK_POST,
        GD_COMMIT_HOOK_MAX
} glusterd_commit_hook_type_t;

typedef struct hooks_private {
        struct cds_list_head    list;
        int                     waitcount; //debug purposes
        pthread_mutex_t         mutex;
        pthread_cond_t          cond;
        pthread_t               worker;
} glusterd_hooks_private_t;

typedef struct hooks_stub {
        struct cds_list_head    all_hooks;
        char                    *scriptdir;
        glusterd_op_t           op;
        dict_t                  *op_ctx;

} glusterd_hooks_stub_t;


static inline gf_boolean_t
is_key_glusterd_hooks_friendly (char *key)
{
        gf_boolean_t is_friendly = _gf_false;

        /* This is very specific to hooks friendly behavior */
        if (fnmatch (GD_HOOKS_SPECIFIC_KEY, key, FNM_NOESCAPE) == 0) {
                gf_msg_debug (THIS->name, 0, "user namespace key %s", key);
                is_friendly = _gf_true;
        }

        return is_friendly;
}

int
glusterd_hooks_create_hooks_directory (char *basedir);

char *
glusterd_hooks_get_hooks_cmd_subdir (glusterd_op_t op);

int
glusterd_hooks_run_hooks (char *hooks_path, glusterd_op_t op, dict_t *op_ctx,
                          glusterd_commit_hook_type_t type);
int
glusterd_hooks_spawn_worker (xlator_t *this);

int
glusterd_hooks_stub_init (glusterd_hooks_stub_t **stub, char *scriptdir,
                          glusterd_op_t op, dict_t *op_ctx);
void
glusterd_hooks_stub_cleanup (glusterd_hooks_stub_t *stub);

int
glusterd_hooks_post_stub_enqueue (char *scriptdir, glusterd_op_t op,
                                      dict_t *op_ctx);
int
glusterd_hooks_priv_init (glusterd_hooks_private_t **new);
#endif
