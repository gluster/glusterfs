#ifndef GF_METADISP_FOPS_H_
#define GF_METADISP_FOPS_H_

#include <glusterfs/xlator.h>
#include <glusterfs/dict.h>
#include <glusterfs/glusterfs.h>

#include <sys/types.h>

/* fops in here are defined in their own file. Every other fop is just defined
 * inline of fops.c */

int
metadisp_readdir(call_frame_t *frame, xlator_t *this, fd_t *fd, size_t size,
                 off_t off, dict_t *xdata);

int
metadisp_readdirp(call_frame_t *frame, xlator_t *this, fd_t *fd, size_t size,
                  off_t off, dict_t *dict);

int
metadisp_lookup(call_frame_t *frame, xlator_t *this, loc_t *loc, dict_t *xdata);

int
metadisp_create(call_frame_t *frame, xlator_t *this, loc_t *loc, int32_t flags,
                mode_t mode, mode_t umask, fd_t *fd, dict_t *xdata);

int
metadisp_open(call_frame_t *frame, xlator_t *this, loc_t *loc, int32_t flags,
              fd_t *fd, dict_t *xdata);

int
metadisp_stat(call_frame_t *frame, xlator_t *this, loc_t *loc, dict_t *xdata);

int
metadisp_inodelk(call_frame_t *frame, xlator_t *this, const char *volume,
                 loc_t *loc, int32_t cmd, struct gf_flock *lock, dict_t *xdata);

int
metadisp_fsync(call_frame_t *frame, xlator_t *this, fd_t *fd, int32_t flags,
               dict_t *xdata);

int
metadisp_unlink(call_frame_t *frame, xlator_t *this, loc_t *loc, int xflag,
                dict_t *xdata);

int
metadisp_setattr(call_frame_t *frame, xlator_t *this, loc_t *loc,
                 struct iatt *stbuf, int32_t valid, dict_t *xdata);

#endif
