/*
  Copyright (c) 2015 Red Hat, Inc. <http://www.redhat.com>
  This file is part of GlusterFS.

  This file is licensed to you under your choice of the GNU Lesser
  General Public License, version 3 or any later version (LGPLv3 or
  later), or the GNU General Public License, version 2 (GPLv2), in all
  cases as published by the Free Software Foundation.
*/

#ifndef _TIER_COMMON_H_
#define _TIER_COMMON_H_

int32_t
tier_readdirp (call_frame_t *frame,
               xlator_t *this,
               fd_t     *fd,
               size_t    size, off_t off, dict_t *dict);

int
tier_readdir (call_frame_t *frame,
              xlator_t *this, fd_t *fd, size_t size,
              off_t yoff, dict_t *xdata);

#endif

