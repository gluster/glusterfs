/*
  Copyright (c) 2008-2015 Red Hat, Inc. <http://www.redhat.com>
  This file is part of GlusterFS.

  This file is licensed to you under your choice of the GNU Lesser
  General Public License, version 3 or any later version (LGPLv3 or
  later), or the GNU General Public License, version 2 (GPLv2), in all
  cases as published by the Free Software Foundation.
*/

#ifndef __TW_H__
#define __TW_H__

#include "xlator.h"
#include "glusterfs.h"

int
glusterfs_global_timer_wheel_init (glusterfs_ctx_t *);

struct tvec_base *
glusterfs_global_timer_wheel (xlator_t *);

#endif /* __TW_H__ */
