/*
  Copyright (c) 2013 Red Hat, Inc. <http://www.redhat.com>
  This file is part of GlusterFS.

  This file is licensed to you under your choice of the GNU Lesser
  General Public License, version 3 or any later version (LGPLv3 or
  later), or the GNU General Public License, version 2 (GPLv2), in all
  cases as published by the Free Software Foundation.
*/

#ifndef _AFR_SELF_HEALD_H
#define _AFR_SELF_HEALD_H

#include <pthread.h>

typedef struct {
    char *path;
    int child;
} shd_event_t;

typedef struct {
    uint64_t healed_count;
    uint64_t split_brain_count;
    uint64_t heal_failed_count;

    /* If start_time is 0, it means crawler is not in progress
       and stats are not valid */
    time_t start_time;
    /* If start_time is NOT 0 and end_time is 0, it means
       cralwer is in progress */
    time_t end_time;
    char *crawl_type;
    int child;
} crawl_event_t;

struct subvol_healer {
    xlator_t *this;
    crawl_event_t crawl_event;
    pthread_mutex_t mutex;
    pthread_cond_t cond;
    pthread_t thread;
    int subvol;
    gf_boolean_t local;
    gf_boolean_t running;
    gf_boolean_t rerun;
};

typedef struct {
    struct subvol_healer *index_healers;
    struct subvol_healer *full_healers;

    eh_t *split_brain;
    eh_t **statistics;
    time_t timeout;
    uint32_t max_threads;
    uint32_t wait_qlength;
    uint32_t halo_max_latency_msec;
    gf_boolean_t iamshd;
    gf_boolean_t enabled;
} afr_self_heald_t;

int
afr_selfheal_daemon_init(xlator_t *this);

int
afr_xl_op(xlator_t *this, dict_t *input, dict_t *output);

int
afr_shd_gfid_to_path(xlator_t *this, xlator_t *subvol, uuid_t gfid,
                     char **path_p);

int
afr_shd_entry_purge(xlator_t *subvol, inode_t *inode, char *name,
                    ia_type_t type);
#endif /* !_AFR_SELF_HEALD_H */
