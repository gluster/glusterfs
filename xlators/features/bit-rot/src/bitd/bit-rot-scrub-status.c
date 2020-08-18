/*
  Copyright (c) 2016 Red Hat, Inc. <http://www.redhat.com>
  This file is part of GlusterFS.

  This file is licensed to you under your choice of the GNU Lesser
  General Public License, version 3 or any later version (LGPLv3 or
  later), or the GNU General Public License, version 2 (GPLv2), in all
  cases as published by the Free Software Foundation.
*/

#include <string.h>
#include <stdio.h>

#include "bit-rot-scrub-status.h"

void
br_inc_unsigned_file_count(br_scrub_stats_t *scrub_stat)
{
    if (!scrub_stat)
        return;

    pthread_mutex_lock(&scrub_stat->lock);
    {
        scrub_stat->unsigned_files++;
    }
    pthread_mutex_unlock(&scrub_stat->lock);
}

void
br_inc_scrubbed_file(br_scrub_stats_t *scrub_stat)
{
    if (!scrub_stat)
        return;

    pthread_mutex_lock(&scrub_stat->lock);
    {
        scrub_stat->scrubbed_files++;
    }
    pthread_mutex_unlock(&scrub_stat->lock);
}

void
br_update_scrub_start_time(br_scrub_stats_t *scrub_stat, time_t time)
{
    if (!scrub_stat)
        return;

    pthread_mutex_lock(&scrub_stat->lock);
    {
        scrub_stat->scrub_start_time = time;
    }
    pthread_mutex_unlock(&scrub_stat->lock);
}

void
br_update_scrub_finish_time(br_scrub_stats_t *scrub_stat, char *timestr,
                            time_t time)
{
    int lst_size = 0;

    if (!scrub_stat)
        return;

    lst_size = sizeof(scrub_stat->last_scrub_time);
    if (strlen(timestr) >= lst_size)
        return;

    pthread_mutex_lock(&scrub_stat->lock);
    {
        scrub_stat->scrub_end_time = time;

        scrub_stat->scrub_duration = scrub_stat->scrub_end_time -
                                     scrub_stat->scrub_start_time;

        snprintf(scrub_stat->last_scrub_time, lst_size, "%s", timestr);
    }
    pthread_mutex_unlock(&scrub_stat->lock);
}
