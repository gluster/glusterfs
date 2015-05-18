/*
  Copyright (c) 2008-2012 Red Hat, Inc. <http://www.redhat.com>
  This file is part of GlusterFS.

  This file is licensed to you under your choice of the GNU Lesser
  General Public License, version 3 or any later version (LGPLv3 or
  later), or the GNU General Public License, version 2 (GPLv2), in all
  cases as published by the Free Software Foundation.
*/

#ifndef __HASHFN_H__
#define __HASHFN_H__

#include <sys/types.h>
#include <stdint.h>

uint32_t SuperFastHash (const char * data, int32_t len);

uint32_t gf_dm_hashfn (const char *msg, int len);

uint32_t ReallySimpleHash (char *path, int len);
#endif /* __HASHFN_H__ */
