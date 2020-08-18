/*
   Copyright (c) 2016 Red Hat, Inc. <http://www.redhat.com>
   This file is part of GlusterFS.

   This file is licensed to you under your choice of the GNU Lesser
   General Public License, version 3 or any later version (LGPLv3 or
   later), or the GNU General Public License, version 2 (GPLv2), in all
   cases as published by the Free Software Foundation.
*/

#ifndef __BIT_ROT_SCRUB_STATUS_H__
#define __BIT_ROT_SCRUB_STATUS_H__

#include <stdint.h>
#include <sys/time.h>
#include <pthread.h>

#include <glusterfs/common-utils.h>

struct br_scrub_stats {
    uint64_t scrubbed_files; /* Total number of scrubbed files. */

    uint64_t unsigned_files; /* Total number of unsigned files. */

    uint64_t scrub_duration; /* Duration of last scrub. */

    char last_scrub_time[GF_TIMESTR_SIZE]; /* Last scrub completion time. */

    time_t scrub_start_time; /* Scrubbing starting time. */

    time_t scrub_end_time; /* Scrubbing finishing time. */

    int8_t scrub_running; /* Whether scrub running or not. */

    pthread_mutex_t lock;
};

typedef struct br_scrub_stats br_scrub_stats_t;

void
br_inc_unsigned_file_count(br_scrub_stats_t *scrub_stat);
void
br_inc_scrubbed_file(br_scrub_stats_t *scrub_stat);
void
br_update_scrub_start_time(br_scrub_stats_t *scrub_stat, time_t time);
void
br_update_scrub_finish_time(br_scrub_stats_t *scrub_stat, char *timestr,
                            time_t time);

#endif /* __BIT_ROT_SCRUB_STATUS_H__ */
