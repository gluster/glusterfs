/*
  Copyright (c) 2015 Red Hat, Inc. <http://www.redhat.com>
  This file is part of GlusterFS.

  This file is licensed to you under your choice of the GNU Lesser
  General Public License, version 3 or any later version (LGPLv3 or
  later), or the GNU General Public License, version 2 (GPLv2), in all
  cases as published by the Free Software Foundation.
*/


#ifndef _CLUSTER_SYNCOP_H
#define _CLUSTER_SYNCOP_H

#include "xlator.h"
#include <sys/time.h>
#include <pthread.h>
#include <ucontext.h>
#include "defaults.h"
#include "default-args.h"
#include "syncop.h"

typedef struct cluster_local_ {
        default_args_cbk_t *replies;
        syncbarrier_t barrier;
} cluster_local_t;

int32_t
cluster_lookup (xlator_t **subvols, unsigned char *on, int numsubvols,
                default_args_cbk_t *replies, unsigned char *output,
                call_frame_t *frame, xlator_t *this, loc_t *loc, dict_t *xdata);
int32_t
cluster_setattr (xlator_t **subvols, unsigned char *on, int numsubvols,
                 default_args_cbk_t *replies, unsigned char *output,
                 call_frame_t *frame, xlator_t *this, loc_t *loc,
                 struct iatt *stbuf, int32_t valid, dict_t *xdata);
int32_t
cluster_getxattr (xlator_t **subvols, unsigned char *on, int numsubvols,
                  default_args_cbk_t *replies, unsigned char *output,
                  call_frame_t *frame, xlator_t *this, loc_t *loc,
                  const char *name, dict_t *xdata);
int32_t
cluster_setxattr (xlator_t **subvols, unsigned char *on, int numsubvols,
                  default_args_cbk_t *replies, unsigned char *output,
                  call_frame_t *frame, xlator_t *this, loc_t *loc, dict_t *dict,
                  int32_t flags, dict_t *xdata);

int
cluster_inodelk (xlator_t **subvols, unsigned char *on, int numsubvols,
                 default_args_cbk_t *replies, unsigned char *locked_on,
                 call_frame_t *frame, xlator_t *this, char *dom,
                 inode_t *inode, off_t off, size_t size);

int
cluster_uninodelk (xlator_t **subvols, unsigned char *locked_on, int numsubvols,
                   default_args_cbk_t *replies, unsigned char *output,
                   call_frame_t *frame, xlator_t *this, char *dom,
                   inode_t *inode, off_t off, size_t size);

int
cluster_entrylk (xlator_t **subvols, unsigned char *on, int numsubvols,
                 default_args_cbk_t *replies, unsigned char *locked_on,
                 call_frame_t *frame, xlator_t *this, char *dom,
                 inode_t *inode, const char *name);

int32_t
cluster_rmdir (xlator_t **subvols, unsigned char *on, int numsubvols,
               default_args_cbk_t *replies, unsigned char *output,
               call_frame_t *frame, xlator_t *this, loc_t *loc, int flags,
               dict_t *xdata);

int32_t
cluster_unlink (xlator_t **subvols, unsigned char *on, int numsubvols,
                default_args_cbk_t *replies, unsigned char *output,
                call_frame_t *frame, xlator_t *this, loc_t *loc, int xflag,
                dict_t *xdata);

int
cluster_mkdir (xlator_t **subvols, unsigned char *on, int numsubvols,
               default_args_cbk_t *replies, unsigned char *output,
               call_frame_t *frame, xlator_t *this, loc_t *loc, mode_t mode,
               mode_t umask, dict_t *xdata);

int32_t
cluster_readlink (xlator_t **subvols, unsigned char *on, int numsubvols,
                  default_args_cbk_t *replies, unsigned char *output,
                  call_frame_t *frame, xlator_t *this, loc_t *loc, size_t size,
                  dict_t *xdata);

int
cluster_symlink (xlator_t **subvols, unsigned char *on, int numsubvols,
                 default_args_cbk_t *replies, unsigned char *output,
                 call_frame_t *frame, xlator_t *this, const char *linkpath,
                 loc_t *loc, mode_t umask, dict_t *xdata);

int32_t
cluster_link (xlator_t **subvols, unsigned char *on, int numsubvols,
              default_args_cbk_t *replies, unsigned char *output,
              call_frame_t *frame, xlator_t *this, loc_t *oldloc, loc_t *newloc,
              dict_t *xdata);

int
cluster_mknod (xlator_t **subvols, unsigned char *on, int numsubvols,
               default_args_cbk_t *replies, unsigned char *output,
               call_frame_t *frame, xlator_t *this, loc_t *loc, mode_t mode,
               dev_t rdev, mode_t umask, dict_t *xdata);

int
cluster_unentrylk (xlator_t **subvols, unsigned char *locked_on, int numsubvols,
                   default_args_cbk_t *replies, unsigned char *output,
                   call_frame_t *frame, xlator_t *this, char *dom,
                   inode_t *inode, const char *name);

int
cluster_tryentrylk (xlator_t **subvols, unsigned char *on, int numsubvols,
                    default_args_cbk_t *replies, unsigned char *locked_on,
                    call_frame_t *frame, xlator_t *this, char *dom,
                    inode_t *inode, const char *name);

int32_t
cluster_fxattrop (xlator_t **subvols, unsigned char *on, int numsubvols,
                  default_args_cbk_t *replies, unsigned char *output,
                  call_frame_t *frame, xlator_t *this, fd_t *fd,
                  gf_xattrop_flags_t flags, dict_t *dict, dict_t *xdata);

int32_t
cluster_xattrop (xlator_t **subvols, unsigned char *on, int numsubvols,
                 default_args_cbk_t *replies, unsigned char *output,
                 call_frame_t *frame, xlator_t *this, loc_t *loc,
                 gf_xattrop_flags_t flags, dict_t *dict, dict_t *xdata);

int32_t
cluster_fstat (xlator_t **subvols, unsigned char *on, int numsubvols,
               default_args_cbk_t *replies, unsigned char *output,
               call_frame_t *frame, xlator_t *this, fd_t *fd, dict_t *xdata);

int32_t
cluster_ftruncate (xlator_t **subvols, unsigned char *on, int numsubvols,
                   default_args_cbk_t *replies, unsigned char *output,
                   call_frame_t *frame, xlator_t *this, fd_t *fd, off_t offset,
                   dict_t *xdata);

int32_t
cluster_open (xlator_t **subvols, unsigned char *on, int numsubvols,
              default_args_cbk_t *replies, unsigned char *output,
              call_frame_t *frame, xlator_t *this, loc_t *loc, int32_t flags,
              fd_t *fd, dict_t *xdata);

int
cluster_tryinodelk (xlator_t **subvols, unsigned char *on, int numsubvols,
                    default_args_cbk_t *replies, unsigned char *locked_on,
                    call_frame_t *frame, xlator_t *this, char *dom,
                    inode_t *inode, off_t off, size_t size);

int32_t
cluster_fsetattr (xlator_t **subvols, unsigned char *on, int numsubvols,
                  default_args_cbk_t *replies, unsigned char *output,
                  call_frame_t *frame, xlator_t *this, fd_t *fd,
                  struct iatt *stbuf, int32_t valid, dict_t *xdata);

void
cluster_replies_wipe (default_args_cbk_t *replies, int num_subvols);
#endif /* !_CLUSTER_SYNCOP_H */
