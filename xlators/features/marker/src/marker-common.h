/*
   Copyright (c) 2008-2012 Red Hat, Inc. <http://www.redhat.com>
   This file is part of GlusterFS.

   This file is licensed to you under your choice of the GNU Lesser
   General Public License, version 3 or any later version (LGPLv3 or
   later), or the GNU General Public License, version 2 (GPLv2), in all
   cases as published by the Free Software Foundation.
*/
#ifndef _MARKER_COMMON_H
#define _MARKER_COMMON_H

#include "marker.h"

int32_t
marker_force_inode_ctx_get(inode_t *, xlator_t *, marker_inode_ctx_t **);

#endif
