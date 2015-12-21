/*
   Copyright (c) 2008-2012 Red Hat, Inc. <http://www.redhat.com>
   This file is part of GlusterFS.

   This file is licensed to you under your choice of the GNU Lesser
   General Public License, version 3 or any later version (LGPLv3 or
   later), or the GNU General Public License, version 2 (GPLv2), in all
   cases as published by the Free Software Foundation.
*/

#ifndef __IO_STATS_MEM_TYPES_H__
#define __IO_STATS_MEM_TYPES_H__

#include "mem-types.h"

extern const char *__progname;

enum gf_io_stats_mem_types_ {
        gf_io_stats_mt_ios_conf = gf_common_mt_end + 1,
        gf_io_stats_mt_ios_fd,
        gf_io_stats_mt_ios_stat,
        gf_io_stats_mt_ios_stat_list,
        gf_io_stats_mt_ios_sample_buf,
        gf_io_stats_mt_ios_sample,
        gf_io_stats_mt_end
};
#endif

