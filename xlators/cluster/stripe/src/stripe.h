/*
  Copyright (c) 2009 Z RESEARCH, Inc. <http://www.zresearch.com>
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
#include <fnmatch.h>
#include <signal.h>


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
};

/**
 * Used to keep info about the replies received from fops->readv calls 
 */
struct readv_replies {
        struct iovec *vector;
        int32_t       count;    //count of vector
        int32_t       op_ret;   //op_ret of readv
        int32_t       op_errno;
        struct stat   stbuf;    /* 'stbuf' is also a part of reply */
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
        struct stat          stbuf;
        struct readv_replies *replies;
        struct statvfs       statvfs_buf;
        dir_entry_t         *entry;
        struct xlator_stats  stats;

        int8_t               revalidate;
        int8_t               failed;
        int8_t               unwind;

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

        /* General usage */
        off_t                offset;
        off_t                stripe_size;

        int xattr_self_heal_needed;
        int entry_self_heal_needed;

        int8_t              *list;
        struct flock         lock;
        fd_t                *fd;
        void                *value;
        struct iobref       *iobref;
};

typedef struct stripe_local   stripe_local_t;
typedef struct stripe_private stripe_private_t;

#endif /* _STRIPE_H_ */
