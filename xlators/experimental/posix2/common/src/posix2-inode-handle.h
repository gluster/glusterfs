/*
   Copyright (c) 2017 Red Hat, Inc. <http://www.redhat.com>
   This file is part of GlusterFS.

   This file is licensed to you under your choice of the GNU Lesser
   General Public License, version 3 or any later version (LGPLv3 or
   later), or the GNU General Public License, version 2 (GPLv2), in all
   cases as published by the Free Software Foundation.
*/
#ifndef _POSIX_INODE_HANDLE_H
#define _POSIX_INODE_HANDLE_H

#include <limits.h>
#include <sys/types.h>
#include "xlator.h"
#include "gf-dirent.h"
#include "posix.h"
#include "posix2-helpers.h"

/* From Open Group Base Specifications Issue 6 */
#ifndef _XOPEN_PATH_MAX
#define _XOPEN_PATH_MAX 1024
#endif

#define TRASH_DIR "landfill"

#define UUID0_STR "00000000-0000-0000-0000-000000000000"
#define SLEN(str) (sizeof(str) - 1)

#define LOC_HAS_ABSPATH(loc) (loc && (loc->path) && (loc->path[0] == '/'))
#define LOC_IS_DIR(loc) (loc && (loc->inode) && \
                (loc->inode->ia_type == IA_IFDIR))

#define MAKE_REAL_PATH(var, this, path) do {                            \
        size_t path_len = strlen(path);                                 \
        size_t var_len = path_len + POSIX_BASE_PATH_LEN(this) + 1;      \
        if (POSIX_PATH_MAX(this) != -1 &&                               \
            var_len >= POSIX_PATH_MAX(this)) {                          \
                var = alloca (path_len + 1);                            \
                strcpy (var, (path[0] == '/') ? path + 1 : path);       \
        } else {                                                        \
                var = alloca (var_len);                                 \
                strcpy (var, POSIX_BASE_PATH(this));                    \
                strcpy (&var[POSIX_BASE_PATH_LEN(this)], path);         \
        }                                                               \
    } while (0)

#define MAKE_HANDLE_PATH(var, this, gfid, base) do {                    \
        int __len;                                                      \
        __len = posix_handle_path (this, gfid, base, NULL, 0);          \
        if (__len <= 0)                                                 \
                break;                                                  \
        var = alloca (__len);                                           \
        __len = posix_handle_path (this, gfid, base, var, __len);       \
        if (__len <= 0)                                                 \
                var = NULL;                                             \
        } while (0)

/*
In: this (xlator_t *), loc (loc_t *)
Out: rpath (char *), iatt_p (struct iatt *)

Intention is to build the rpath (or the real path), given a loc. In the
case of making INODE handles, this is just the GFID based path for POSIX2.
Once the rpath is built, it is also checked if present, and the corresponding
stat information passed back in iatt_p.

rpath for POSIX2 on disk structure would be,
    - .glusterfs/[0..9|a..f][0..9|a..f]/[0..9|a..f][0..9|a..f]/GFID

rpath is further allocated on the stack (alloca), which is why this is a MACRO
*/
#define MAKE_INODE_HANDLE(rpath, this, loc, iatt_p) do {                \
        int reqlen, retlen;                                             \
        struct posix_private *priv = NULL;                              \
        if (gf_uuid_is_null (loc->gfid)) {                              \
                gf_msg (this->name, GF_LOG_ERROR, 0,                    \
                        P_MSG_INODE_HANDLE_CREATE,                      \
                        "Missing gfid for inode resolution");           \
                op_ret = -1;                                            \
                errno = EINVAL;                                         \
                break;                                                  \
        }                                                               \
        errno = 0;                                                      \
        priv = this->private;                                           \
        reqlen = posix2_handle_length (priv->base_path_length);         \
        rpath = alloca (reqlen);                                        \
        retlen = posix2_make_handle (loc->gfid, priv->base_path,        \
                                     rpath, reqlen);                    \
        if (retlen <= reqlen) {                                         \
                op_ret = posix2_istat_path (this, loc->gfid, rpath,     \
                                            iatt_p, _gf_false);         \
        } else {                                                        \
                gf_msg (this->name, GF_LOG_ERROR, 0,                    \
                        P_MSG_INODE_HANDLE_CREATE,                      \
                        "Unable to make handle for inode %s",           \
                        uuid_utoa ((loc)->gfid));                       \
                op_ret = -1;                                            \
                errno = ENOMEM;                                         \
                break;                                                  \
        }                                                               \
        } while (0)


#define POSIX_ANCESTRY_PATH (1 << 0)
#define POSIX_ANCESTRY_DENTRY (1 << 1)

int
posix_handle_path (xlator_t *, uuid_t, const char *, char *, size_t);

int
posix_make_ancestryfromgfid (xlator_t *, char *, int, gf_dirent_t *, int,
                             uuid_t, const size_t, const char *,
                             inode_table_t *, inode_t **, dict_t *, int32_t *);

int
posix_handle_init (xlator_t *);

int
posix_handle_trash_init (xlator_t *);

#endif /* !_POSIX_INODE_HANDLE_H */
