/*
   Copyright (c) 2011 Gluster, Inc. <http://www.gluster.com>
   This file is part of GlusterFS.

   GlusterFS is free software; you can redistribute it and/or modify
   it under the terms of the GNU General Public License as published
   by the Free Software Foundation; either version 3 of the License,
   or (at your option) any later version.

   GlusterFS is distributed in the hope that it will be useful, but
   WITHOUT ANY WARRANTY; without even the implied warranty of
   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
   General Public License for more details.

   You should have received a copy of the GNU General Public License
   along with this program.  If not, see
   <http://www.gnu.org/licenses/>.
*/

#ifndef _POSIX_HANDLE_H
#define _POSIX_HANDLE_H

#ifndef _CONFIG_H
#define _CONFIG_H
#include "config.h"
#endif

#include <sys/types.h>
#include "xlator.h"


#define LOC_HAS_ABSPATH(loc) ((loc) && (loc->path) && (loc->path[0] == '/'))

#define MAKE_REAL_PATH(var, this, path) do {                            \
        var = alloca (strlen (path) + POSIX_BASE_PATH_LEN(this) + 2);   \
        strcpy (var, POSIX_BASE_PATH(this));                            \
        strcpy (&var[POSIX_BASE_PATH_LEN(this)], path);                 \
        } while (0)


#define MAKE_HANDLE_PATH(var, this, gfid, base) do {                    \
        int __len;                                                      \
        __len = posix_handle_path (this, gfid, base, NULL, 0);          \
        if (__len <= 0)                                                 \
                break;                                                  \
        var = alloca (__len);                                           \
        __len = posix_handle_path (this, gfid, base, var, __len);       \
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


#define MAKE_INODE_HANDLE(rpath, this, loc, iatt_p) do {                \
        if (uuid_is_null (loc->gfid)) {                                 \
                gf_log (this->name, GF_LOG_ERROR,                       \
                        "null gfid for path %s", loc->path);            \
                break;                                                  \
        }                                                               \
        errno = 0;                                                      \
        op_ret = posix_istat (this, loc->gfid, NULL, iatt_p);           \
        if (errno != ELOOP) {                                           \
                MAKE_HANDLE_PATH (rpath, this, loc->gfid, NULL);        \
                break;                                                  \
        }                                                               \
        /* __ret == -1 && errno == ELOOP */                             \
        if (LOC_HAS_ABSPATH (loc)) {                                    \
                MAKE_REAL_PATH (rpath, this, loc->path);                \
                op_ret = posix_pstat (this, loc->gfid, rpath, iatt_p);  \
                break;                                                  \
        }                                                               \
        } while (0)


#define MAKE_ENTRY_HANDLE(entp, parp, this, loc, ent_p) do {            \
        char *__parp;                                                   \
                                                                        \
        if (uuid_is_null (loc->pargfid) || !loc->name) {                \
                gf_log (this->name, GF_LOG_ERROR,                       \
                        "null pargfid/name for path %s", loc->path);    \
                break;                                                  \
        }                                                               \
                                                                        \
        errno = 0;                                                      \
        op_ret = posix_istat (this, loc->pargfid, loc->name, ent_p);    \
        if (errno != ELOOP) {                                           \
                MAKE_HANDLE_PATH (parp, this, loc->pargfid, NULL);      \
                MAKE_HANDLE_PATH (entp, this, loc->pargfid, loc->name); \
                break;                                                  \
        }                                                               \
        /* __ret == -1 && errno == ELOOP */                             \
        if (LOC_HAS_ABSPATH (loc)) {                                    \
                MAKE_REAL_PATH (entp, this, loc->path);                 \
                __parp = strdupa (entp);                                \
                parp = dirname (__parp);                                \
                op_ret = posix_pstat (this, NULL, entp, ent_p);         \
                break;                                                  \
        }                                                               \
        /* expand ELOOP */                                              \
        } while (0)



int
posix_handle_path (xlator_t *this, uuid_t gfid, const char *basename, char *buf,
                   size_t len);
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

#endif /* !_POSIX_HANDLE_H */
