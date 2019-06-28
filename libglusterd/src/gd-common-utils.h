/*
  Copyright (c) 2019 Red Hat, Inc. <http://www.redhat.com>
  This file is part of GlusterFS.

  This file is licensed to you under your choice of the GNU Lesser
  General Public License, version 3 or any later version (LGPLv3 or
  later), or the GNU General Public License, version 2 (GPLv2), in all
  cases as published by the Free Software Foundation.
*/

#ifndef _GD_COMMON_UTILS_H
#define _GD_COMMON_UTILS_H

#include <fcntl.h>
#include <unistd.h>
#include <limits.h>
#include <stddef.h>

#include "protocol-common.h"
#include "rpcsvc.h"

int
get_vol_type(int type, int dist_count, int brick_count);

char *
get_struct_variable(int mem_num, gf_gsync_status_t *sts_val);

#endif /* _GD_COMMON_UTILS_H */
