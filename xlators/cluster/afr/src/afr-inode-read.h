/*
  Copyright (c) 2008-2012 Red Hat, Inc. <http://www.redhat.com>
  This file is part of GlusterFS.

  This file is licensed to you under your choice of the GNU Lesser
  General Public License, version 3 or any later version (LGPLv3 or
  later), or the GNU General Public License, version 2 (GPLv2), in all
  cases as published by the Free Software Foundation.
*/

#ifndef __INODE_READ_H__
#define __INODE_READ_H__

int32_t
afr_access (call_frame_t *frame, xlator_t *this,
	    loc_t *loc, int32_t mask, dict_t *xdata);

int32_t
afr_stat (call_frame_t *frame, xlator_t *this,
	  loc_t *loc, dict_t *xdata);

int32_t
afr_fstat (call_frame_t *frame, xlator_t *this,
	   fd_t *fd, dict_t *xdata);

int32_t
afr_readlink (call_frame_t *frame, xlator_t *this,
	      loc_t *loc, size_t size, dict_t *xdata);

int32_t
afr_readv (call_frame_t *frame, xlator_t *this,
	   fd_t *fd, size_t size, off_t offset, uint32_t flags, dict_t *xdata);

int32_t
afr_getxattr (call_frame_t *frame, xlator_t *this,
	      loc_t *loc, const char *name, dict_t *xdata);

int32_t
afr_fgetxattr (call_frame_t *frame, xlator_t *this,
               fd_t *fd, const char *name, dict_t *xdata);


int
afr_handle_quota_size (call_frame_t *frame, xlator_t *this);
#endif /* __INODE_READ_H__ */
