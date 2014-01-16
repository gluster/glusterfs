/*
   Copyright (c) 2013 Red Hat, Inc. <http://www.redhat.com>
   This file is part of GlusterFS.

   This file is licensed to you under your choice of the GNU Lesser
   General Public License, version 3 or any later version (LGPLv3 or
   later), or the GNU General Public License, version 2 (GPLv2), in all
   cases as published by the Free Software Foundation.
*/
#ifndef __GFID_ACCESS_H__
#define __GFID_ACCESS_H__

#ifndef _CONFIG_H
#define _CONFIG_H
#include "config.h"
#endif

#include "glusterfs.h"
#include "logging.h"
#include "dict.h"
#include "xlator.h"
#include "defaults.h"
#include "gfid-access-mem-types.h"

#define UUID_CANONICAL_FORM_LEN 36

#define GF_FUSE_AUX_GFID_NEWFILE "glusterfs.gfid.newfile"
#define GF_FUSE_AUX_GFID_HEAL    "glusterfs.gfid.heal"

#define GF_GFID_KEY "GLUSTERFS_GFID"
#define GF_GFID_DIR ".gfid"
#define GF_AUX_GFID 0xd

#define GFID_ACCESS_GET_VALID_DIR_INODE(x,l,unref,lbl) do {             \
                int       ret       = 0;                                \
                uint64_t  value     = 0;                                \
                inode_t  *tmp_inode = NULL;                             \
                                                                        \
                /* if its an entry operation, on the virtual */         \
                /* directory inode as parent, we need to handle */      \
                /* it properly */                                       \
                if (l->parent) {                                        \
                        ret = inode_ctx_get (l->parent, x, &value);     \
                        if (ret)                                        \
                                goto lbl;                               \
                        tmp_inode = (inode_t *)value;                   \
                        l->parent = inode_ref (tmp_inode);                          \
                        /* if parent is virtual, no need to handle */   \
                        /* loc->inode */                                \
                        break;                                          \
                }                                                       \
                                                                        \
                /* if its an inode operation, on the virtual */         \
                /* directory inode itself, we need to handle */         \
                /* it properly */                                       \
                if (l->inode) {                                         \
                        ret = inode_ctx_get (l->inode, x, &value);      \
                        if (ret)                                        \
                                goto lbl;                               \
                        tmp_inode = (inode_t *)value;                   \
                        l->inode = inode_ref (tmp_inode);                           \
                }                                                       \
                                                                        \
        } while (0)

#define GFID_ACCESS_ENTRY_OP_CHECK(loc,err,lbl)    do {                 \
                /* need to check if the lookup is on virtual dir */     \
                if ((loc->name && !strcmp (GF_GFID_DIR, loc->name)) &&  \
                    ((loc->parent &&                                    \
                      __is_root_gfid (loc->parent->gfid)) ||            \
                      __is_root_gfid (loc->pargfid))) {                 \
                        err = EEXIST;                                   \
                        goto lbl;                                       \
                }                                                       \
                                                                        \
                /* now, check if the lookup() is on an existing */      \
                /* entry, but on gfid-path */                           \
                if ((loc->parent &&                                     \
                     __is_gfid_access_dir (loc->parent->gfid)) ||       \
                    __is_gfid_access_dir (loc->pargfid)) {              \
                        err = EPERM;                                    \
                        goto lbl;                                       \
                }                                                       \
        } while (0)


typedef struct {
        unsigned int  uid;
        unsigned int  gid;
        char          gfid[UUID_CANONICAL_FORM_LEN + 1];
        unsigned int  st_mode;
        char         *bname;

        union {
                struct _symlink_in {
                        char     *linkpath;
                } __attribute__ ((__packed__)) symlink;

                struct _mknod_in {
                        unsigned int mode;
                        unsigned int rdev;
                        unsigned int umask;
                } __attribute__ ((__packed__)) mknod;

                struct _mkdir_in {
                        unsigned int mode;
                        unsigned int umask;
                } __attribute__ ((__packed__)) mkdir;
        } __attribute__ ((__packed__)) args;
} __attribute__((__packed__)) ga_newfile_args_t;

typedef struct {
        char      gfid[UUID_CANONICAL_FORM_LEN + 1];
        char     *bname; /* a null terminated basename */
} __attribute__((__packed__)) ga_heal_args_t;

struct ga_private {
        /* root inode's stbuf */
        struct iatt root_stbuf;
        struct iatt gfiddir_stbuf;
        struct mem_pool *newfile_args_pool;
        struct mem_pool *heal_args_pool;
};
typedef struct ga_private ga_private_t;

struct __ga_local {
        call_frame_t *orig_frame;
        unsigned int uid;
        unsigned int gid;
        loc_t loc;
};
typedef struct __ga_local ga_local_t;

#endif /* __GFID_ACCESS_H__ */
