/*
   Copyright (c) 2006-2010 Gluster, Inc. <http://www.gluster.com>
   This file is part of GlusterFS.

   GlusterFS is free software; you can redistribute it and/or modify
   it under the terms of the GNU Affero General Public License as published
   by the Free Software Foundation; either version 3 of the License,
   or (at your option) any later version.

   GlusterFS is distributed in the hope that it will be useful, but
   WITHOUT ANY WARRANTY; without even the implied warranty of
   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
   Affero General Public License for more details.

   You should have received a copy of the GNU Affero General Public License
   along with this program.  If not, see
   <http://www.gnu.org/licenses/>.
*/

#ifndef _GF_FUSE_BRIDGE_H_
#define _GF_FUSE_BRIDGE_H_

#include <stdint.h>
#include <signal.h>
#include <pthread.h>
#include <stddef.h>
#include <dirent.h>
#include <sys/mount.h>
#include <sys/time.h>
#include <fnmatch.h>

#ifndef _CONFIG_H
#define _CONFIG_H
#include "config.h"
#endif /* _CONFIG_H */

#include "glusterfs.h"
#include "logging.h"
#include "xlator.h"
#include "defaults.h"
#include "common-utils.h"
#include "glusterfsd-common.h"
#include "statedump.h"

#ifdef GF_DARWIN_HOST_OS
/* This is MacFUSE's marker for MacFUSE-specific code */
#define __FreeBSD__ 10
#include "fuse_kernel_macfuse.h"
#else
#include "fuse_kernel.h"
#endif
#include "fuse-misc.h"
#include "fuse-mount.h"
#include "fuse-mem-types.h"

#include "list.h"
#include "dict.h"

/* TODO: when supporting posix acl, remove this definition */
#define DISABLE_POSIX_ACL

#ifdef GF_LINUX_HOST_OS
#define FUSE_OP_HIGH (FUSE_POLL + 1)
#endif
#ifdef GF_DARWIN_HOST_OS
#define FUSE_OP_HIGH (FUSE_DESTROY + 1)
#endif
#define GLUSTERFS_XATTR_LEN_MAX  65536

#define MAX_FUSE_PROC_DELAY 1

typedef struct fuse_in_header fuse_in_header_t;
typedef void (fuse_handler_t) (xlator_t *this, fuse_in_header_t *finh,
                               void *msg);

struct fuse_private {
        int                  fd;
        uint32_t             proto_minor;
        char                *volfile;
        size_t               volfile_size;
        char                *mount_point;
        struct iobuf        *iobuf;

        pthread_t            fuse_thread;
        char                 fuse_thread_started;

        uint32_t             direct_io_mode;
        size_t              *msg0_len_p;

        double               entry_timeout;
        double               attribute_timeout;

        pthread_cond_t       sync_cond;
        pthread_mutex_t      sync_mutex;
        char                 child_up;

        char                 init_recvd;

        gf_boolean_t         strict_volfile_check;

        fuse_handler_t     **fuse_ops;
        fuse_handler_t     **fuse_ops0;
        pthread_mutex_t      fuse_dump_mutex;
        int                  fuse_dump_fd;

        glusterfs_graph_t   *next_graph;
        xlator_t            *active_subvol;

        pid_t                client_pid;
        gf_boolean_t         client_pid_set;
};
typedef struct fuse_private fuse_private_t;

#define _FH_TO_FD(fh) ((fd_t *)(uintptr_t)(fh))

#define FH_TO_FD(fh) ((_FH_TO_FD (fh))?(fd_ref (_FH_TO_FD (fh))):((fd_t *) 0))

#define FUSE_FOP(state, ret, op_num, fop, args ...)                     \
        do {                                                            \
                call_frame_t *frame = NULL;                             \
                xlator_t *xl = NULL;                                    \
                                                                        \
                frame = get_call_frame_for_req (state);                 \
                if (!frame) {                                           \
                         /* This is not completely clean, as some       \
                          * earlier allocations might remain unfreed    \
                          * if we return at this point, but still       \
                          * better than trying to go on with a NULL     \
                          * frame ...                                   \
                          */                                            \
                        gf_log ("glusterfs-fuse",                       \
                                GF_LOG_ERROR,                           \
                                "FUSE message"                          \
                                " unique %"PRIu64" opcode %d:"          \
                                " frame allocation failed",             \
                                state->finh->unique,                    \
                                state->finh->opcode);                   \
                        free_fuse_state (state);                        \
                        return;                                         \
                }                                                       \
                                                                        \
                frame->root->state = state;                             \
                frame->root->op    = op_num;                            \
                frame->op          = op_num;                            \
                                                                        \
                xl = fuse_state_subvol (state);                         \
                if (!xl) {                                              \
                        gf_log ("glusterfs-fuse", GF_LOG_ERROR,         \
                                "xl is NULL");                          \
                        send_fuse_err (state->this, state->finh, ENOENT); \
                        free_fuse_state (state);                        \
                        STACK_DESTROY (frame->root);                    \
                } else {                                                \
                        STACK_WIND (frame, ret, xl, xl->fops->fop, args); \
                }                                                       \
        } while (0)


#define FUSE_FOP_COOKIE(state, xl, ret, cky, op_num, fop, args ...)     \
        do {                                                            \
                call_frame_t *frame = NULL;                             \
                                                                        \
                frame = get_call_frame_for_req (state);                 \
                if (!frame) {                                           \
                        gf_log ("glusterfs-fuse",                       \
                                GF_LOG_ERROR,                           \
                                "FUSE message"                          \
                                " unique %"PRIu64" opcode %d:"          \
                                " frame allocation failed",             \
                                state->finh->unique,                    \
                                state->finh->opcode);                   \
                        free_fuse_state (state);                        \
                        return 0;                                       \
                }                                                       \
                                                                        \
                frame->root->state = state;                             \
                frame->root->op    = op_num;                            \
		frame->op          = op_num;				\
                STACK_WIND_COOKIE (frame, ret, cky, xl, xl->fops->fop, args); \
        } while (0)

#define GF_SELECT_LOG_LEVEL(_errno)                     \
        (((_errno == ENOENT) || (_errno == ESTALE))?    \
         GF_LOG_DEBUG)

#define GET_STATE(this, finh, state)                                       \
        do {                                                               \
                state = get_fuse_state (this, finh);                       \
                if (!state) {                                              \
                        gf_log ("glusterfs-fuse",                          \
                                GF_LOG_ERROR,                              \
                                "FUSE message unique %"PRIu64" opcode %d:" \
                                " state allocation failed",                \
                                finh->unique, finh->opcode);               \
                                                                           \
                        send_fuse_err (this, finh, ENOMEM);                \
                        GF_FREE (finh);                                    \
                                                                           \
                        return;                                            \
                }                                                          \
        } while (0)



typedef enum {
        RESOLVE_MUST = 1,
        RESOLVE_NOT,
        RESOLVE_MAY,
        RESOLVE_DONTCARE,
        RESOLVE_EXACT
} fuse_resolve_type_t;

struct fuse_resolve_comp {
        char      *basename;
        ino_t      ino;
        uint64_t   gen;
        inode_t   *inode;
};

typedef struct {
        fuse_resolve_type_t    type;
        ino_t                  ino;
        uint64_t               gen;
        ino_t                  par;
        fd_t                  *fd;
        char                  *path;
        char                  *bname;
        u_char                 gfid[16];
        u_char                 pargfid[16];
	char                  *resolved;
        int                    op_ret;
        int                    op_errno;
        loc_t                  deep_loc;
        struct fuse_resolve_comp *components;
        int                    comp_count;
} fuse_resolve_t;


typedef struct {
        void             *pool;
        xlator_t         *this;
        inode_table_t    *itable;
        loc_t             loc;
        loc_t             loc2;
        fuse_in_header_t *finh;
        int32_t           flags;
        off_t             off;
        size_t            size;
        unsigned long     nlookup;
        fd_t             *fd;
        dict_t           *dict;
        char             *name;
        char              is_revalidate;
        gf_boolean_t      truncate_needed;
        gf_lock_t         lock;
        uint64_t          lk_owner;

        /* used within resolve_and_resume */
        /* */
        fuse_resolve_t resolve;
        fuse_resolve_t resolve2;

        loc_t        *loc_now;
        fuse_resolve_t *resolve_now;

        void *resume_fn;

        int            valid;
        int            mask;
        dev_t          rdev;
        mode_t         mode;
        struct iatt    attr;
        struct gf_flock   lk_lock;
        struct iovec   vector;

        uuid_t         gfid;
} fuse_state_t;

typedef void (*fuse_resume_fn_t) (fuse_state_t *state);

GF_MUST_CHECK int32_t
fuse_loc_fill (loc_t *loc, fuse_state_t *state, ino_t ino,
               ino_t par, const char *name);
call_frame_t *get_call_frame_for_req (fuse_state_t *state);
fuse_state_t *get_fuse_state (xlator_t *this, fuse_in_header_t *finh);
void free_fuse_state (fuse_state_t *state);
void gf_fuse_stat2attr (struct iatt *st, struct fuse_attr *fa);
uint64_t inode_to_fuse_nodeid (inode_t *inode);
xlator_t *fuse_state_subvol (fuse_state_t *state);
xlator_t *fuse_active_subvol (xlator_t *fuse);
inode_t *fuse_ino_to_inode (uint64_t ino, xlator_t *fuse);
int fuse_resolve_and_resume (fuse_state_t *state, fuse_resume_fn_t fn);
int send_fuse_err (xlator_t *this, fuse_in_header_t *finh, int error);
int fuse_gfid_set (fuse_state_t *state);
#endif /* _GF_FUSE_BRIDGE_H_ */
