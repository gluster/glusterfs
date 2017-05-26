/*
 *   Copyright (c) 2016 Red Hat, Inc. <http://www.redhat.com>
 *   This file is part of GlusterFS.
 *
 *   This file is licensed to you under your choice of the GNU Lesser
 *   General Public License, version 3 or any later version (LGPLv3 or
 *   later), or the GNU General Public License, version 2 (GPLv2), in all
 *   cases as published by the Free Software Foundation.
 */

#ifndef __NL_CACHE_H__
#define __NL_CACHE_H__

#include "nl-cache-mem-types.h"
#include "nl-cache-messages.h"
#include "glusterfs.h"
#include "xlator.h"
#include "defaults.h"
#include "atomic.h"

#define NLC_INVALID 0x0000
#define NLC_PE_FULL 0x0001
#define NLC_PE_PARTIAL 0x0002
#define NLC_NE_VALID 0x0004

#define IS_PE_VALID(state) ((state != NLC_INVALID) && \
                            (state & (NLC_PE_FULL | NLC_PE_PARTIAL)))
#define IS_NE_VALID(state) ((state != NLC_INVALID) && (state & NLC_NE_VALID))

#define IS_PEC_ENABLED(conf) (conf->positive_entry_cache)
#define IS_CACHE_ENABLED(conf) ((!conf->cache_disabled))

#define NLC_STACK_UNWIND(fop, frame, params ...) do {       \
        nlc_local_t *__local = NULL;                        \
        xlator_t *__xl      = NULL;                         \
        if (frame) {                                        \
                __xl = frame->this;                         \
                __local = frame->local;                     \
                frame->local = NULL;                        \
        }                                                   \
        STACK_UNWIND_STRICT (fop, frame, params);           \
        nlc_local_wipe (__xl, __local);                     \
} while (0)

enum nlc_cache_clear_reason {
        NLC_NONE = 0,
        NLC_LRU_PRUNE,
};

struct nlc_ne {
        struct list_head  list;
        char             *name;
};
typedef struct nlc_ne nlc_ne_t;

struct nlc_pe {
        struct list_head  list;
        inode_t          *inode;
        char             *name;
};
typedef struct nlc_pe nlc_pe_t;

struct nlc_timer_data {
        inode_t          *inode;
        xlator_t         *this;
};
typedef struct nlc_timer_data nlc_timer_data_t;

struct nlc_lru_node {
        inode_t          *inode;
        struct list_head  list;
};
typedef struct nlc_lru_node nlc_lru_node_t;

struct nlc_ctx {
        struct list_head         pe;   /* list of positive entries */
        struct list_head         ne;   /* list of negative entries */
        uint64_t                 state;
        time_t                   cache_time;
        struct gf_tw_timer_list *timer;
        nlc_timer_data_t         *timer_data;
        size_t                   cache_size;
        uint64_t                 refd_inodes;
        gf_lock_t                lock;
};
typedef struct nlc_ctx nlc_ctx_t;

struct nlc_local {
        loc_t    loc;
        loc_t    loc2;
        inode_t *inode;
        inode_t *parent;
        fd_t    *fd;
        char    *linkname;
        glusterfs_fop_t fop;
};
typedef struct nlc_local nlc_local_t;

struct nlc_statistics {
        gf_atomic_t nlc_hit; /* No. of times lookup/stat was served from this xl */
        gf_atomic_t nlc_miss; /* No. of times negative lookups were sent to disk */
        /* More granular counters */
        gf_atomic_t nameless_lookup;
        gf_atomic_t getrealfilename_hit;
        gf_atomic_t getrealfilename_miss;
        gf_atomic_t pe_inode_cnt;
        gf_atomic_t ne_inode_cnt;
        gf_atomic_t nlc_invals; /* No. of invalidates recieved from upcall*/
};

struct nlc_conf {
        int32_t              cache_timeout;
        gf_boolean_t         positive_entry_cache;
        gf_boolean_t         negative_entry_cache;
        gf_boolean_t         disable_cache;
        uint64_t             cache_size;
        gf_atomic_t          current_cache_size;
        uint64_t             inode_limit;
        gf_atomic_t          refd_inodes;
        struct tvec_base    *timer_wheel;
        time_t               last_child_down;
        struct list_head     lru;
        gf_lock_t            lock;
        struct nlc_statistics nlc_counter;
};
typedef struct nlc_conf nlc_conf_t;

gf_boolean_t
nlc_get_real_file_name (xlator_t *this, loc_t *loc, const char *fname,
                        int32_t *op_ret, int32_t *op_errno, dict_t *dict);

gf_boolean_t
nlc_is_negative_lookup (xlator_t *this, loc_t *loc);

void
nlc_set_dir_state (xlator_t *this, inode_t *inode, uint64_t state);

void
nlc_dir_add_pe (xlator_t *this, inode_t *inode, inode_t *entry_ino,
                const char *name);

void
nlc_dir_remove_pe (xlator_t *this, inode_t *inode, inode_t *entry_ino,
                   const char *name, gf_boolean_t multilink);

void
nlc_dir_add_ne (xlator_t *this, inode_t *inode, const char *name);

void
nlc_local_wipe (xlator_t *this, nlc_local_t *local);

nlc_local_t *
nlc_local_init (call_frame_t *frame, xlator_t *this, glusterfs_fop_t fop,
                loc_t *loc, loc_t *loc2);

void
nlc_update_child_down_time (xlator_t *this, time_t *now);

void
nlc_inode_clear_cache (xlator_t *this, inode_t *inode,
                      int reason);

void
nlc_dump_inodectx (xlator_t *this, inode_t *inode);

void
nlc_clear_all_cache (xlator_t *this);

void
nlc_disable_cache (xlator_t *this);

void
nlc_lru_prune (xlator_t *this, inode_t *inode);

#endif /* __NL_CACHE_H__ */
