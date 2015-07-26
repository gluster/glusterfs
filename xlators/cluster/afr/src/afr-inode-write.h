/*
  Copyright (c) 2008-2012 Red Hat, Inc. <http://www.redhat.com>
  This file is part of GlusterFS.

  This file is licensed to you under your choice of the GNU Lesser
  General Public License, version 3 or any later version (LGPLv3 or
  later), or the GNU General Public License, version 2 (GPLv2), in all
  cases as published by the Free Software Foundation.
*/

#ifndef __INODE_WRITE_H__
#define __INODE_WRITE_H__

int32_t
afr_chmod (call_frame_t *frame, xlator_t *this,
	   loc_t *loc, mode_t mode, dict_t *xdata);

int32_t
afr_chown (call_frame_t *frame, xlator_t *this,
	   loc_t *loc, uid_t uid, gid_t gid, dict_t *xdata);

int
afr_fchown (call_frame_t *frame, xlator_t *this,
	    fd_t *fd, uid_t uid, gid_t gid, dict_t *xdata);

int32_t
afr_fchmod (call_frame_t *frame, xlator_t *this,
	    fd_t *fd, mode_t mode, dict_t *xdata);

int32_t
afr_writev (call_frame_t *frame, xlator_t *this, fd_t *fd,
	    struct iovec *vector, int32_t count, off_t offset,
            uint32_t flags, struct iobref *iobref, dict_t *xdata);

int32_t
afr_truncate (call_frame_t *frame, xlator_t *this,
	      loc_t *loc, off_t offset, dict_t *xdata);

int32_t
afr_ftruncate (call_frame_t *frame, xlator_t *this,
	       fd_t *fd, off_t offset, dict_t *xdata);

int32_t
afr_utimens (call_frame_t *frame, xlator_t *this,
	     loc_t *loc, struct timespec tv[2], dict_t *xdata);

int
afr_setattr (call_frame_t *frame, xlator_t *this,
             loc_t *loc, struct iatt *buf, int32_t valid, dict_t *xdata);

int
afr_fsetattr (call_frame_t *frame, xlator_t *this,
              fd_t *fd, struct iatt *buf, int32_t valid, dict_t *xdata);

int32_t
afr_setxattr (call_frame_t *frame, xlator_t *this,
              loc_t *loc, dict_t *dict, int32_t flags, dict_t *xdata);

int32_t
afr_fsetxattr (call_frame_t *frame, xlator_t *this,
               fd_t *fd, dict_t *dict, int32_t flags, dict_t *xdata);

int32_t
afr_removexattr (call_frame_t *frame, xlator_t *this,
		 loc_t *loc, const char *name, dict_t *xdata);

int32_t
afr_fremovexattr (call_frame_t *frame, xlator_t *this,
                  fd_t *fd, const char *name, dict_t *xdata);

int
afr_discard (call_frame_t *frame, xlator_t *this, fd_t *fd, off_t offset,
             size_t len, dict_t *xdata);

int
afr_fallocate (call_frame_t *frame, xlator_t *this, fd_t *fd, int32_t mode,
               off_t offset, size_t len, dict_t *xdata);

int
afr_zerofill(call_frame_t *frame, xlator_t *this, fd_t *fd, off_t offset,
             off_t len, dict_t *xdata);

int32_t
afr_xattrop (call_frame_t *frame, xlator_t *this, loc_t *loc,
             gf_xattrop_flags_t optype, dict_t *xattr, dict_t *xdata);

int32_t
afr_fxattrop (call_frame_t *frame, xlator_t *this, fd_t *fd,
              gf_xattrop_flags_t optype, dict_t *xattr, dict_t *xdata);
#endif /* __INODE_WRITE_H__ */
