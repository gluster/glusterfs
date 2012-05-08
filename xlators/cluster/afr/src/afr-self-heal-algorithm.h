/*
  Copyright (c) 2008-2012 Red Hat, Inc. <http://www.redhat.com>
  This file is part of GlusterFS.

  This file is licensed to you under your choice of the GNU Lesser
  General Public License, version 3 or any later version (LGPLv3 or
  later), or the GNU General Public License, version 2 (GPLv2), in all
  cases as published by the Free Software Foundation.
*/

#ifndef __AFR_SELF_HEAL_ALGORITHM_H__
#define __AFR_SELF_HEAL_ALGORITHM_H__

typedef int (*afr_sh_algo_fn) (call_frame_t *frame,
                               xlator_t *this);

struct afr_sh_algorithm {
        const char *name;
        afr_sh_algo_fn fn;
};

extern struct afr_sh_algorithm afr_self_heal_algorithms[3];
typedef struct {
        gf_lock_t lock;
        unsigned int loops_running;
        off_t offset;

        int32_t total_blocks;
        int32_t diff_blocks;
} afr_sh_algo_private_t;

#endif /* __AFR_SELF_HEAL_ALGORITHM_H__ */
