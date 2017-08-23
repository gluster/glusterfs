/*
  Copyright (c) 2016 Red Hat, Inc. <http://www.redhat.com>
  This file is part of GlusterFS.

  This file is licensed to you under your choice of the GNU Lesser
  General Public License, version 3 or any later version (LGPLv3 or
  later), or the GNU General Public License, version 2 (GPLv2), in all
  cases as published by the Free Software Foundation.
*/

/* File: rio-common-helpers.h
 * This file contains common rio helper routines
 */

#ifndef _RIO_COMMON_HELPERS_H
#define _RIO_COMMON_HELPERS_H

#include "list.h"
#include "layout.h"
#include "rio-common.h"

int rio_process_volume_lists (xlator_t *, struct rio_conf *);
void rio_destroy_volume_lists (struct rio_conf *);
int32_t rio_lookup_is_nameless (loc_t *);

#endif /* _RIO_COMMON_HELPERS_H */
