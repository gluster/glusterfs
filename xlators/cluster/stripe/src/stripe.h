/*
  Copyright (c) 2010 Gluster, Inc. <http://www.gluster.com>
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


#ifndef _STRIPE_H_
#define _STRIPE_H_

#ifndef _CONFIG_H
#define _CONFIG_H
#include "config.h"
#endif

#include "xlator.h"
#include "logging.h"
#include "defaults.h"
#include "common-utils.h"
#include "compat.h"
#include "compat-errno.h"
#include "stripe-mem-types.h"
#include "libxlator.h"
#include <fnmatch.h>
#include <signal.h>

#define STRIPE_PATHINFO_HEADER "STRIPE:"


#define STRIPE_STACK_UNWIND(fop, frame, params ...) do {           \
                stripe_local_t *__local = NULL;                    \
                if (frame) {                                    \
                        __local = frame->local;                 \
                        frame->local = NULL;                    \
                }                                               \
                STACK_UNWIND_STRICT (fop, frame, params);       \
                if (__local) {                                  \
                        stripe_local_wipe(__local);             \
                        GF_FREE (__local);                      \
                }                                               \
        } while (0)

#define STRIPE_STACK_DESTROY(frame) do {                  \
                stripe_local_t *__local = NULL;           \
                __local = frame->local;                   \
                frame->local = NULL;                      \
                STACK_DESTROY (frame->root);              \
                if (__local) {                            \
                        stripe_local_wipe (__local);      \
                        GF_FREE (__local);                \
                }                                         \
        } while (0)

typedef struct stripe_xattr_sort {
        int32_t  pos;
        int32_t  pathinfo_len;
        char    *pathinfo;
} stripe_xattr_sort_t;

/**
 * struct stripe_options : This keeps the pattern and the block-size
 *     information, which is used for striping on a file.
 */
struct stripe_options {
        struct stripe_options *next;
        char                   path_pattern[256];
        uint64_t               block_size;
};

/**
 * Private structure for stripe translator
 */
struct stripe_private {
        struct stripe_options  *pattern;
        xlator_t              **xl_array;
        uint64_t                block_size;
        gf_lock_t               lock;
        uint8_t                 nodes_down;
        int8_t                  first_child_down;
        int8_t                  child_count;
        int8_t                 *state; /* Current state of child node */
        gf_boolean_t            xattr_supported;  /* default yes */
        char                    vol_uuid[UUID_SIZE + 1];
};

/**
 * Used to keep info about the replies received from fops->readv calls
 */
struct readv_replies {
        struct iovec *vector;
        int32_t       count;    //count of vector
        int32_t       op_ret;   //op_ret of readv
        int32_t       op_errno;
        int32_t       requested_size;
        struct iatt   stbuf;    /* 'stbuf' is also a part of reply */
};

typedef struct _stripe_fd_ctx {
        off_t      stripe_size;
        int        stripe_count;
        int        static_array;
        xlator_t **xl_array;
} stripe_fd_ctx_t;


/**
 * Local structure to be passed with all the frames in case of STACK_WIND
 */
struct stripe_local; /* this itself is used inside the structure; */

struct stripe_local {
        struct stripe_local *next;
        call_frame_t        *orig_frame;

        stripe_fd_ctx_t     *fctx;

        /* Used by _cbk functions */
        struct iatt          stbuf;
        struct iatt          pre_buf;
        struct iatt          post_buf;
        struct iatt          preparent;
        struct iatt          postparent;

        off_t                stbuf_size;
        off_t                prebuf_size;
        off_t                postbuf_size;
        off_t                preparent_size;
        off_t                postparent_size;

        blkcnt_t             stbuf_blocks;
        blkcnt_t             prebuf_blocks;
        blkcnt_t             postbuf_blocks;
        blkcnt_t             preparent_blocks;
        blkcnt_t             postparent_blocks;

        struct readv_replies *replies;
        struct statvfs        statvfs_buf;
        dir_entry_t          *entry;

        int8_t               revalidate;
        int8_t               failed;
        int8_t               unwind;

        size_t               readv_size;
        int32_t              entry_count;
        int32_t              node_index;
        int32_t              call_count;
        int32_t              wind_count; /* used instead of child_cound
                                            in case of read and write */
        int32_t              op_ret;
        int32_t              op_errno;
        int32_t              count;
        int32_t              flags;
        char                *name;
        inode_t             *inode;

        loc_t                loc;
        loc_t                loc2;

        /* For File I/O fops */
        dict_t              *dict;

        stripe_xattr_sort_t *xattr_list;
        int32_t              xattr_total_len;
        int32_t              nallocs;

        struct marker_str    marker;

        /* General usage */
        off_t                offset;
        off_t                stripe_size;

        int xattr_self_heal_needed;
        int entry_self_heal_needed;

        int8_t              *list;
        struct gf_flock         lock;
        fd_t                *fd;
        void                *value;
        struct iobref       *iobref;
        gf_dirent_t          entries;
        dict_t              *xattr;
        uuid_t               ia_gfid;
};

typedef struct stripe_local   stripe_local_t;
typedef struct stripe_private stripe_private_t;


#endif /* _STRIPE_H_ */
