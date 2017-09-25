/*
  Copyright (c) 2013 Red Hat, Inc. <http://www.redhat.com>
  This file is part of GlusterFS.

  This file is licensed to you under your choice of the GNU Lesser
  General Public License, version 3 or any later version (LGPLv3 or
  later), or the GNU General Public License, version 2 (GPLv2), in all
  cases as published by the Free Software Foundation.
*/

#include <stdio.h>
#include <inttypes.h>
#include <time.h>
#include <sys/time.h>
#include <string.h>

#if defined GF_DARWIN_HOST_OS
#include <mach/mach_time.h>
static mach_timebase_info_data_t gf_timebase;
#endif

#include "logging.h"
#include "timespec.h"
#include "libglusterfs-messages.h"
#include "common-utils.h"

void timespec_now (struct timespec *ts)
{
#if defined GF_LINUX_HOST_OS || defined GF_SOLARIS_HOST_OS || defined GF_BSD_HOST_OS
        if (0 == clock_gettime(CLOCK_MONOTONIC, ts)) {
                /* All good */
                return;
        }

        /* Fall back, but there is hope in gettimeofday() syscall */
        struct timeval tv;
        if (0 == gettimeofday(&tv, NULL)) {
                /* Again, all good */
                TIMEVAL_TO_TIMESPEC(&tv, ts);
                return;
        }

        /* If control hits here, there is surely a problem,
           mainly because, as per man page too, these syscalls
           shouldn't fail. Best way is to ABORT, because it is
           not right */
        GF_ABORT ("gettimeofday() failed!!");

#elif defined GF_DARWIN_HOST_OS
        uint64_t time = mach_absolute_time();
        static double scaling = 0.0;

        if (mach_timebase_info(&gf_timebase) != KERN_SUCCESS) {
                gf_timebase.numer = 1;
                gf_timebase.denom = 1;
        }
        if (gf_timebase.denom == 0) {
                gf_timebase.numer = 1;
                gf_timebase.denom = 1;
        }

        scaling = (double) gf_timebase.numer / (double) gf_timebase.denom;
        time *= scaling;

        ts->tv_sec = (time * NANO);
        ts->tv_nsec = (time - (ts->tv_sec * GIGA));

#endif /* Platform verification */
}

void timespec_adjust_delta (struct timespec *ts, struct timespec delta)
{
        ts->tv_nsec = ((ts->tv_nsec + delta.tv_nsec) % 1000000000);
        ts->tv_sec += ((ts->tv_nsec + delta.tv_nsec) / 1000000000);
        ts->tv_sec += delta.tv_sec;
}

void timespec_sub (const struct timespec *begin, const struct timespec *end,
                   struct timespec *res)
{
        if (end->tv_nsec < begin->tv_nsec) {
                res->tv_sec = end->tv_sec - begin->tv_sec - 1;
                res->tv_nsec = end->tv_nsec + 1000000000 - begin->tv_nsec;
        } else {
                res->tv_sec = end->tv_sec - begin->tv_sec;
                res->tv_nsec = end->tv_nsec - begin->tv_nsec;
        }
}
