/*
  Copyright (c) 2008-2012 Red Hat, Inc. <http://www.redhat.com>
  This file is part of GlusterFS.

  This file is licensed to you under your choice of the GNU Lesser
  General Public License, version 3 or any later version (LGPLv3 or
  later), or the GNU General Public License, version 2 (GPLv2), in all
  cases as published by the Free Software Foundation.
*/

#ifndef __CHECKSUM_H__
#define __CHECKSUM_H__

uint32_t
gf_rsync_weak_checksum (unsigned char *buf, size_t len);

void
gf_rsync_strong_checksum (unsigned char *buf, size_t len, unsigned char *sum);

#endif /* __CHECKSUM_H__ */
