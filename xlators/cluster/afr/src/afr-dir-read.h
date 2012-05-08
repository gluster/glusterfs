/*
  Copyright (c) 2008-2012 Red Hat, Inc. <http://www.redhat.com>
  This file is part of GlusterFS.

  This file is licensed to you under your choice of the GNU Lesser
  General Public License, version 3 or any later version (LGPLv3 or
  later), or the GNU General Public License, version 2 (GPLv2), in all
  cases as published by the Free Software Foundation.
*/

#ifndef __DIR_READ_H__
#define __DIR_READ_H__


int32_t
afr_opendir (call_frame_t *frame, xlator_t *this,
	     loc_t *loc, fd_t *fd, dict_t *xdata);

int32_t
afr_releasedir (xlator_t *this, fd_t *fd);

int32_t
afr_readdir (call_frame_t *frame, xlator_t *this,
	     fd_t *fd, size_t size, off_t offset, dict_t *xdata);


int32_t
afr_readdirp (call_frame_t *frame, xlator_t *this,
              fd_t *fd, size_t size, off_t offset, dict_t *dict);

int32_t
afr_checksum (call_frame_t *frame, xlator_t *this,
	      loc_t *loc, int32_t flags, dict_t *xdata);


#endif /* __DIR_READ_H__ */
