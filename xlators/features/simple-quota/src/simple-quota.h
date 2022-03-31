/*
   Copyright (c) 2020 Kadalu.IO <https://kadalu.io>
   This file is part of GlusterFS.

   This file is licensed to you under your choice of the GNU Lesser
   General Public License, version 3 or any later version (LGPLv3 or
   later), or the GNU General Public License, version 2 (GPLv2), in all
   cases as published by the Free Software Foundation.
*/
#ifndef __SIMPLE_QUOTA__
#define __SIMPLE_QUOTA__

typedef struct {
    gf_lock_t lock;
    pthread_t quota_set_thread;
    struct list_head ns_list;
    bool no_distribute;
    bool use_backend;
    bool take_cmd_from_all_client;
    bool allow_fops;
} sq_private_t;

typedef struct {
    struct list_head priv_list; /* list of ns entris in private */
    inode_t *ns;                /* namespace inode */
    gf_atomic_t pending_update;
    int64_t xattr_size;
    int64_t hard_lim;
    int64_t total_size;

    /* total files used, will be useful only when using backend quota */
    int64_t total_files;
} sq_inode_t;

#endif /* __SIMPLE_QUOTA_H__ */
