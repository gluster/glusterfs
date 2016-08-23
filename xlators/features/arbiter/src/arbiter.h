/*
  Copyright (c) 2015 Red Hat, Inc. <http://www.redhat.com>
  This file is part of GlusterFS.

  This file is licensed to you under your choice of the GNU Lesser
  General Public License, version 3 or any later version (LGPLv3 or
  later), or the GNU General Public License, version 2 (GPLv2), in all
  cases as published by the Free Software Foundation.
*/

#ifndef _ARBITER_H
#define _ARBITER_H

#include "locking.h"
#include "common-utils.h"

typedef struct arbiter_inode_ctx_ {
        struct iatt iattbuf;
} arbiter_inode_ctx_t;

#endif /* _ARBITER_H */
