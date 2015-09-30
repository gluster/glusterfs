/*
   Copyright (c) 2014 Red Hat, Inc. <http://www.redhat.com>
   This file is part of GlusterFS.

   This file is licensed to you under your choice of the GNU Lesser
   General Public License, version 3 or any later version (LGPLv3 or
   later), or the GNU General Public License, version 2 (GPLv2), in all
   cases as published by the Free Software Foundation.
*/

#ifndef __READONLY_H__
#define __READONLY_H__

#include "read-only-mem-types.h"
#include "xlator.h"

typedef struct {
        gf_boolean_t      readonly_or_worm_enabled;
} read_only_priv_t;

#endif
