/*
  Copyright (c) 2018 Red Hat, Inc. <http://www.redhat.com>
  This file is part of GlusterFS.

  This file is licensed to you under your choice of the GNU Lesser
  General Public License, version 3 or any later version (LGPLv3 or
  later), or the GNU General Public License, version 2 (GPLv2), in all
  cases as published by the Free Software Foundation.
*/

#ifndef __UTIME_H__
#define __UTIME_H__

#include "utime-autogen-fops.h"

typedef struct utime_priv {
    gf_boolean_t noatime;
} utime_priv_t;

#endif /* __UTIME_H__ */
