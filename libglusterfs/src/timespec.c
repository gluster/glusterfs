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
#if defined GF_LINUX_HOST_OS || defined GF_SOLARIS_HOST_OS || defined GF_BSD_HOST_OS
#include <time.h>
#include <sys/time.h>
#endif

#if defined GF_DARWIN_HOST_OS
#include <mach/mach_time.h>
#endif

#include "logging.h"
#include "time.h"


void tv2ts (struct timeval tv, struct timespec *ts)
{
        ts->tv_sec  = tv.tv_sec;
        ts->tv_nsec = tv.tv_usec * 1000;
}

void timespec_now (struct timespec *ts)
{
#if defined GF_LINUX_HOST_OS || defined GF_SOLARIS_HOST_OS || defined GF_BSD_HOST_OS

        if (0 == clock_gettime(CLOCK_MONOTONIC, ts))
                return;
        else {
                struct timeval tv;
                if (0 == gettimeofday(&tv, NULL))
                        tv2ts(tv, ts);
        }
#elif defined GF_DARWIN_HOST_OS
        mach_timebase_info_data_t tb = { 0 };
        static double timebase = 0.0;
        uint64_t time = 0;
        mach_timebase_info (&tb);

        timebase *= info.numer;
        timebase /= info.denom;

        time = mach_absolute_time();
        time *= timebase;

        ts->tv_sec = (time * NANO);
        ts->tv_nsec = (time - (ts.tv_sec * GIGA));

#endif /* Platform verification */
        gf_log_callingfn ("timer", GF_LOG_DEBUG, "%"PRIu64".%09"PRIu64,
                          ts->tv_sec, ts->tv_nsec);
}

void timespec_adjust_delta (struct timespec *ts, struct timespec delta)
{
        ts->tv_nsec = ((ts->tv_nsec + delta.tv_nsec) % 1000000000);
        ts->tv_sec += ((ts->tv_nsec + delta.tv_nsec) / 1000000000);
        ts->tv_sec += delta.tv_sec;
}
