/*
   Copyright (c) 2011-2012 Red Hat, Inc. <http://www.redhat.com>
   This file is part of GlusterFS.

   This file is licensed to you under your choice of the GNU Lesser
   General Public License, version 3 or any later version (LGPLv3 or
   later), or the GNU General Public License, version 2 (GPLv2), in all
   cases as published by the Free Software Foundation.
*/
#ifndef _POSIX_HANDLE_H
#define _POSIX_HANDLE_H

#include <limits.h>
#include <sys/types.h>
#include "xlator.h"
#include "gf-dirent.h"
#include "posix-messages.h"

/* From Open Group Base Specifications Issue 6 */
#ifndef _XOPEN_PATH_MAX
#define _XOPEN_PATH_MAX 1024
#endif

#define TRASH_DIR "landfill"

#define UUID0_STR "00000000-0000-0000-0000-000000000000"
#define SLEN(str) (sizeof(str) - 1)

#define HANDLE_ABSPATH_LEN(this) (POSIX_BASE_PATH_LEN(this) + \
                                  SLEN("/" GF_HIDDEN_PATH "/00/00/" \
                                  UUID0_STR) + 1)

#define LOC_HAS_ABSPATH(loc) (loc && (loc->path) && (loc->path[0] == '/'))
#define LOC_IS_DIR(loc) (loc && (loc->inode) && \
                (loc->inode->ia_type == IA_IFDIR))

#define MAKE_PGFID_XATTR_KEY(var, prefix, pgfid) do {                   \
        var = alloca (strlen (prefix) + UUID_CANONICAL_FORM_LEN + 1);   \
        strcpy (var, prefix);                                           \
        strcat (var, uuid_utoa (pgfid));                                \
        } while (0)

#define SET_PGFID_XATTR(path, key, value, flags, op_ret, this, label) do {    \
        value = hton32 (value);                                         \
        op_ret = sys_lsetxattr (path, key, &value, sizeof (value),      \
                                flags);                                 \
        if (op_ret == -1) {                                             \
                op_errno = errno;                                       \
                gf_msg (this->name, GF_LOG_WARNING, errno, P_MSG_PGFID_OP, \
                        "setting xattr failed on %s: key = %s ",        \
                        path, key);                                     \
                goto label;                                             \
        }                                                               \
        } while (0)

#define SET_PGFID_XATTR_IF_ABSENT(path, key, value, flags, op_ret, this, label)\
        do {                                                                   \
                op_ret = sys_lgetxattr (path, key, &value, sizeof (value));    \
                if (op_ret == -1) {                                            \
                        op_errno = errno;                                      \
                        if (op_errno == ENOATTR) {                             \
                                value = 1;                                     \
                                SET_PGFID_XATTR (path, key, value, flags,      \
                                                 op_ret, this, label);         \
                        } else {                                               \
                                gf_msg (this->name, GF_LOG_WARNING, op_errno,  \
                                       P_MSG_PGFID_OP, "getting xattr "    \
                                       "failed on %s: key = %s ",              \
                                       path, key);                             \
                        }                                                      \
                }                                                              \
        } while (0)

#define REMOVE_PGFID_XATTR(path, key, op_ret, this, label) do {               \
       op_ret = sys_lremovexattr (path, key);                           \
       if (op_ret == -1) {                                              \
               op_errno = errno;                                        \
               gf_msg (this->name, GF_LOG_WARNING, op_errno,                   \
                       P_MSG_PGFID_OP,                                     \
                       "removing xattr failed"                                 \
                       "on %s: key = %s", path, key);                          \
               goto label;                                              \
       }                                                                \
       } while (0)

/* should be invoked holding a lock */
#define LINK_MODIFY_PGFID_XATTR(path, key, value, flags, op_ret, this, label) do { \
       op_ret = sys_lgetxattr (path, key, &value, sizeof (value));  \
       if (op_ret == -1) {                                              \
               op_errno = errno;                                        \
               if (op_errno == ENOATTR || op_errno == ENODATA) {        \
                       value = 1;                                       \
               } else {                                                 \
                       gf_msg (this->name, GF_LOG_WARNING, errno,       \
                               P_MSG_PGFID_OP, "getting xattr "      \
                               "failed on %s: key = %s ", path, key);      \
                       goto label;                                      \
               }                                                        \
       } else {                                                         \
               value = ntoh32 (value);                                  \
               value++;                                                 \
       }                                                                \
       SET_PGFID_XATTR (path, key, value, flags, op_ret, this, label);  \
       } while (0)

/* should be invoked holding a lock */
#define UNLINK_MODIFY_PGFID_XATTR(path, key, value, flags, op_ret, this, label) do { \
       op_ret = sys_lgetxattr (path, key, &value, sizeof (value));  \
       if (op_ret == -1) {                                              \
               op_errno = errno;                                        \
               gf_msg (this->name, GF_LOG_WARNING, errno,               \
                      P_MSG_PGFID_OP, "getting xattr failed on " \
                       "%s: key = %s ", path, key);                     \
               goto label;                                              \
       } else {                                                         \
               value = ntoh32 (value);                                  \
               value--;                                                 \
               if (value > 0) {                                         \
                       SET_PGFID_XATTR (path, key, value, flags, op_ret, \
                                        this, label);                   \
               } else {                                                 \
                       REMOVE_PGFID_XATTR (path, key, op_ret, this, label); \
               }                                                        \
       }                                                                \
    } while (0)

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


#define MAKE_HANDLE_GFID_PATH(var, this, gfid, base) do {               \
        int __len = 0;                                                  \
        __len = posix_handle_gfid_path (this, gfid, base, NULL, 0);     \
        if (__len <= 0)                                                 \
                break;                                                  \
        var = alloca (__len);                                           \
        __len = posix_handle_gfid_path (this, gfid, base, var, __len);  \
        } while (0)


#define MAKE_HANDLE_RELPATH(var, this, gfid, base) do {                 \
        int __len;                                                      \
        __len = posix_handle_relpath (this, gfid, base, NULL, 0);       \
        if (__len <= 0)                                                 \
                break;                                                  \
        var = alloca (__len);                                           \
        __len = posix_handle_relpath (this, gfid, base, var, __len);    \
        } while (0)


#define MAKE_HANDLE_ABSPATH(var, this, gfid) do {                       \
        struct posix_private * __priv = this->private;                  \
        int __len = HANDLE_ABSPATH_LEN(this);                           \
        var = alloca(__len);                                            \
        snprintf(var, __len, "%s/" GF_HIDDEN_PATH "/%02x/%02x/%s",      \
                 __priv->base_path, gfid[0], gfid[1], uuid_utoa(gfid)); \
        } while (0)


#define MAKE_INODE_HANDLE(rpath, this, loc, iatt_p) do {                \
        if (gf_uuid_is_null (loc->gfid)) {                              \
                gf_msg (this->name, GF_LOG_ERROR, 0,                    \
                        P_MSG_INODE_HANDLE_CREATE,                      \
                        "null gfid for path %s", (loc)->path);          \
                break;                                                  \
        }                                                               \
        if (LOC_IS_DIR (loc) && LOC_HAS_ABSPATH (loc)) {                \
                MAKE_REAL_PATH (rpath, this, (loc)->path);              \
                op_ret = posix_pstat (this, (loc)->gfid, rpath, iatt_p); \
                break;                                                  \
        }                                                               \
        errno = 0;                                                      \
        op_ret = posix_istat (this, loc->gfid, NULL, iatt_p);           \
        if (errno != ELOOP) {                                           \
                MAKE_HANDLE_PATH (rpath, this, (loc)->gfid, NULL);      \
                if (!rpath) {                                           \
                        op_ret = -1;                                    \
                        gf_msg (this->name, GF_LOG_ERROR, errno,        \
                                P_MSG_INODE_HANDLE_CREATE,            \
                                "Failed to create inode handle "        \
                                "for path %s", (loc)->path);            \
                }                                                       \
                break;                                                  \
        }                                                               \
        /* __ret == -1 && errno == ELOOP */                             \
        } while (0)


#define MAKE_ENTRY_HANDLE(entp, parp, this, loc, ent_p) do {            \
        char *__parp;                                                   \
                                                                        \
        if (gf_uuid_is_null (loc->pargfid) || !loc->name) {             \
                gf_msg (this->name, GF_LOG_ERROR, 0, P_MSG_ENTRY_HANDLE_CREATE,\
                        "null pargfid/name for path %s", loc->path);    \
                break;                                                  \
        }                                                               \
                                                                        \
        if (LOC_HAS_ABSPATH (loc)) {                                    \
                MAKE_REAL_PATH (entp, this, loc->path);                 \
                __parp = strdupa (entp);                                \
                parp = dirname (__parp);                                \
                op_ret = posix_pstat (this, NULL, entp, ent_p);         \
                break;                                                  \
        }                                                               \
        errno = 0;                                                      \
        op_ret = posix_istat (this, loc->pargfid, loc->name, ent_p);    \
        if (errno != ELOOP) {                                           \
                MAKE_HANDLE_PATH (parp, this, loc->pargfid, NULL);      \
                MAKE_HANDLE_PATH (entp, this, loc->pargfid, loc->name); \
                if (!parp || !entp) {                                   \
                        gf_msg (this->name, GF_LOG_ERROR, errno,        \
                                P_MSG_ENTRY_HANDLE_CREATE,      \
                                "Failed to create entry handle "        \
                                "for path %s", loc->path);              \
                }                                                       \
                break;                                                  \
        }                                                               \
        /* __ret == -1 && errno == ELOOP */                             \
        /* expand ELOOP */                                              \
        } while (0)


#define POSIX_ANCESTRY_PATH (1 << 0)
#define POSIX_ANCESTRY_DENTRY (1 << 1)

int
posix_handle_path (xlator_t *this, uuid_t gfid, const char *basename, char *buf,
                   size_t len);

int
posix_make_ancestryfromgfid (xlator_t *this, char *path, int pathsize,
                             gf_dirent_t *head, int type, uuid_t gfid,
                             const size_t handle_size,
                             const char *priv_base_path,
                             inode_table_t *table, inode_t **parent,
                             dict_t *xdata, int32_t *op_errno);
int
posix_handle_path_safe (xlator_t *this, uuid_t gfid, const char *basename,
                        char *buf, size_t len);

int
posix_handle_gfid_path (xlator_t *this, uuid_t gfid, const char *basename,
                        char *buf, size_t len);

int
posix_handle_hard (xlator_t *this, const char *path, uuid_t gfid,
                   struct stat *buf);


int
posix_handle_soft (xlator_t *this, const char *real_path, loc_t *loc,
                   uuid_t gfid, struct stat *buf);

int
posix_handle_unset (xlator_t *this, uuid_t gfid, const char *basename);

int posix_handle_mkdir_hashes (xlator_t *this, const char *newpath);

int posix_handle_init (xlator_t *this);

int posix_create_link_if_gfid_exists (xlator_t *this, uuid_t gfid,
                                      char *real_path, inode_table_t *itable);

int
posix_handle_trash_init (xlator_t *this);
#endif /* !_POSIX_HANDLE_H */
