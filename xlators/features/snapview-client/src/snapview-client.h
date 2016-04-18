 /*
   Copyright (c) 2014 Red Hat, Inc. <http://www.redhat.com>
   This file is part of GlusterFS.

   This file is licensed to you under your choice of the GNU Lesser
   General Public License, version 3 or any later version (LGPLv3 or
   later), or the GNU General Public License, version 2 (GPLv2), in all
   cases as published by the Free Software Foundation.
*/
#ifndef __SNAP_VIEW_CLIENT_H__
#define __SNAP_VIEW_CLIENT_H__

#include "glusterfs.h"
#include "logging.h"
#include "dict.h"
#include "xlator.h"
#include "defaults.h"
#include "snapview-client-mem-types.h"

struct __svc_local {
        loc_t loc;
        xlator_t *subvolume;
        fd_t     *fd;
        void *cookie;
        dict_t *xdata;
};
typedef struct __svc_local svc_local_t;

#define SVC_STACK_UNWIND(fop, frame, params ...) do {           \
                svc_local_t *__local = NULL;                    \
                if (frame) {                                    \
                        __local      = frame->local;            \
                        frame->local = NULL;                    \
                }                                               \
                STACK_UNWIND_STRICT (fop, frame, params);       \
                svc_local_free (__local);                       \
        } while (0)

#define SVC_ENTRY_POINT_SET(this, xdata, op_ret, op_errno, new_xdata,   \
                            priv, ret, label)                           \
        do {                                                            \
                if (!xdata) {                                           \
                        xdata = new_xdata = dict_new ();                \
                        if (!new_xdata) {                               \
                                gf_log (this->name, GF_LOG_ERROR,       \
                                        "failed to allocate new dict"); \
                                op_ret = -1;                            \
                                op_errno = ENOMEM;                      \
                                goto label;                             \
                        }                                               \
                }                                                       \
                ret = dict_set_str (xdata, "entry-point", "true");      \
                if (ret) {                                              \
                        gf_log (this->name, GF_LOG_ERROR,               \
                                "failed to set dict");                  \
                        op_ret = -1;                                    \
                        op_errno = ENOMEM;                              \
                        goto label;                                     \
                }                                                       \
        } while (0);

#define SVC_GET_SUBVOL_FROM_CTX(this, op_ret, op_errno, inode_type, ret, \
                                inode, subvolume, label)                \
        do {                                                            \
                ret = svc_inode_ctx_get (this, inode, &inode_type);     \
                if (ret < 0) {                                          \
                        gf_log (this->name, GF_LOG_ERROR,               \
                                "inode context not found for gfid %s",  \
                                uuid_utoa (inode->gfid));               \
                        op_ret = -1;                                    \
                        op_errno = EINVAL;                              \
                        goto label;                                     \
                }                                                       \
                                                                        \
                subvolume = svc_get_subvolume (this, inode_type);       \
        }  while (0);

struct svc_private {
        char *path;
        char *special_dir; /* needed for samba */
        gf_boolean_t show_entry_point;
};
typedef struct svc_private svc_private_t;

struct svc_fd {
        off_t last_offset;
        gf_boolean_t entry_point_handled;
        gf_boolean_t special_dir;
};
typedef struct svc_fd svc_fd_t;

typedef enum {
        NORMAL_INODE = 1,
        VIRTUAL_INODE
} inode_type_t;

#endif /* __SNAP_VIEW_CLIENT_H__ */
