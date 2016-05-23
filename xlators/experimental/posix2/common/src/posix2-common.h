/*
  Copyright (c) 2016 Red Hat, Inc. <http://www.redhat.com>
  This file is part of GlusterFS.

  This file is licensed to you under your choice of the GNU Lesser
  General Public License, version 3 or any later version (LGPLv3 or
  later), or the GNU General Public License, version 2 (GPLv2), in all
  cases as published by the Free Software Foundation.
*/

/* File: posix2-common.h
 * Primary header for posix2-common code.
 */

#ifndef _POSIX2_COMMON_H
#define _POSIX2_COMMON_H

#include "xlator.h"
#include "posix2-mem-types.h"

struct posix2_conf {
        /* memory pools */

        /* subvolume configuration */
        char    *p2cnf_directory; /* path to local brick */
};

int32_t posix2_common_init (xlator_t *);
void posix2_common_fini (xlator_t *);

#endif /* _POSIX2_COMMON_H */
