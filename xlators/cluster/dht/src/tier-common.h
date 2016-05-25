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
/* Function definitions */
int
tier_create_unlink_stale_linkto_cbk (call_frame_t *frame, void *cookie,
                                     xlator_t *this, int op_ret, int op_errno,
                                     struct iatt *preparent,
                                     struct iatt *postparent, dict_t *xdata);

int
tier_create_cbk (call_frame_t *frame, void *cookie, xlator_t *this,
                 int op_ret, int op_errno,
                 fd_t *fd, inode_t *inode, struct iatt *stbuf,
                 struct iatt *preparent, struct iatt *postparent,
                 dict_t *xdata);

int
tier_create_linkfile_create_cbk (call_frame_t *frame, void *cookie,
                                 xlator_t *this,
                                 int32_t op_ret, int32_t op_errno,
                                 inode_t *inode, struct iatt *stbuf,
                                 struct iatt *preparent,
                                 struct iatt *postparent,
                                 dict_t *xdata);

int
tier_create (call_frame_t *frame, xlator_t *this,
             loc_t *loc, int32_t flags, mode_t mode,
             mode_t umask, fd_t *fd, dict_t *params);

int32_t
tier_unlink (call_frame_t *frame, xlator_t *this,
             loc_t *loc, int xflag, dict_t *xdata);

int32_t
tier_readdirp (call_frame_t *frame,
               xlator_t *this,
               fd_t     *fd,
               size_t    size, off_t off, dict_t *dict);

int
tier_readdir (call_frame_t *frame,
              xlator_t *this, fd_t *fd, size_t size,
              off_t yoff, dict_t *xdata);



int
tier_link (call_frame_t *frame, xlator_t *this,
          loc_t *oldloc, loc_t *newloc, dict_t *xdata);


int
tier_statfs (call_frame_t *frame, xlator_t *this, loc_t *loc, dict_t *xdata);


#endif

