#ifndef _AHA_FOPS_H
#define _AHA_FOPS_H

#include "aha.h"
#include "aha-helpers.h"

/* FOP functions */
int
aha_lookup (call_frame_t *frame, xlator_t *this, loc_t *loc, dict_t *xdata);

int
aha_stat (call_frame_t *frame, xlator_t *this, loc_t *loc, dict_t *xdata);

int
aha_setattr (call_frame_t *frame, xlator_t *this, loc_t *loc,
                struct iatt *stbuf, int32_t valid, dict_t *xdata);

int
aha_fsetattr (call_frame_t *frame, xlator_t *this, fd_t *fd,
                struct iatt *stbuf, int32_t valid, dict_t *xdata);

int
aha_truncate (call_frame_t *frame, xlator_t *this, loc_t *loc,
                off_t offset, dict_t *xdata);

int
aha_ftruncate (call_frame_t *frame, xlator_t *this, fd_t *fd,
                off_t offset, dict_t *xdata);

int
aha_access (call_frame_t *frame, xlator_t *this, loc_t *loc, int32_t mask,
                dict_t *xdata);

int
aha_readlink (call_frame_t *frame, xlator_t *this, loc_t *loc, size_t size,
                dict_t *xdata);

int
aha_mknod (call_frame_t *frame, xlator_t *this, loc_t *loc, mode_t mode,
                dev_t rdev, mode_t umask, dict_t *xdata);

int
aha_mkdir (call_frame_t *frame, xlator_t *this, loc_t *loc, mode_t mode,
                mode_t umask, dict_t *xdata);

int
aha_unlink (call_frame_t *frame, xlator_t *this, loc_t *loc, int xflag,
                dict_t *xdata);

int
aha_rmdir (call_frame_t *frame, xlator_t *this, loc_t *loc, int flags,
                dict_t *xdata);

int
aha_symlink (call_frame_t *frame, xlator_t *this, const char *linkpath,
                loc_t *loc, mode_t umask, dict_t *xdata);

int
aha_rename (call_frame_t *frame, xlator_t *this, loc_t *oldloc, loc_t *newloc,
                dict_t *xdata);

int
aha_link (call_frame_t *frame, xlator_t *this, loc_t *oldloc, loc_t *newloc,
                dict_t *xdata);

int
aha_create (call_frame_t *frame, xlator_t *this, loc_t *loc, int32_t flags,
                mode_t mode, mode_t umask, fd_t *fd, dict_t *xdata);

int
aha_open (call_frame_t *frame, xlator_t *this, loc_t *loc, int32_t flags,
                fd_t *fd, dict_t *xdata);

int
aha_readv (call_frame_t *frame, xlator_t *this,
		 fd_t *fd, size_t size, off_t offset, uint32_t flags,
                 dict_t *xdata);

int
aha_writev (call_frame_t *frame, xlator_t *this, fd_t *fd, struct iovec *vector,
                int32_t count, off_t off, uint32_t flags,
                struct iobref *iobref, dict_t *xdata);

int
aha_flush (call_frame_t *frame, xlator_t *this, fd_t *fd, dict_t *xdata);

int
aha_fsync (call_frame_t *frame, xlator_t *this, fd_t *fd,
                int32_t flags, dict_t *xdata);

int
aha_fstat (call_frame_t *frame, xlator_t *this, fd_t *fd, dict_t *xdata);

int
aha_opendir (call_frame_t *frame, xlator_t *this, loc_t *loc, fd_t *fd,
                dict_t *xdata);

int
aha_fsyncdir (call_frame_t *frame, xlator_t *this, fd_t *fd, int32_t flags,
                dict_t *xdata);

int
aha_statfs (call_frame_t *frame, xlator_t *this, loc_t *loc, dict_t *xdata);

int
aha_setxattr (call_frame_t *frame, xlator_t *this, loc_t *loc, dict_t *dict,
                int32_t flags, dict_t *xdata);

int
aha_getxattr (call_frame_t *frame, xlator_t *this, loc_t *loc,
                const char *name, dict_t *xdata);

int
aha_fsetxattr (call_frame_t *frame, xlator_t *this, fd_t *fd,
                dict_t *dict, int32_t flags, dict_t *xdata);

int
aha_fgetxattr (call_frame_t *frame, xlator_t *this, fd_t *fd,
                const char *name, dict_t *xdata);

int
aha_xattrop (call_frame_t *frame, xlator_t *this, loc_t *loc,
                gf_xattrop_flags_t flags, dict_t *dict, dict_t *xdata);

int
aha_fxattrop (call_frame_t *frame, xlator_t *this, fd_t *fd,
                gf_xattrop_flags_t flags, dict_t *dict, dict_t *xdata);

int
aha_removexattr (call_frame_t *frame, xlator_t *this, loc_t *loc,
                        const char *name, dict_t *xdata);

int
aha_fremovexattr (call_frame_t *frame, xlator_t *this, fd_t *fd,
                        const char *name, dict_t *xdata);

int
aha_lk (call_frame_t *frame, xlator_t *this, fd_t *fd, int32_t cmd,
        struct gf_flock *lock, dict_t *xdata);

int
aha_inodelk (call_frame_t *frame, xlator_t *this, const char *volume,
                loc_t *loc, int32_t cmd, struct gf_flock *lock,
                dict_t *xdata);

int
aha_finodelk (call_frame_t *frame, xlator_t *this,
		    const char *volume, fd_t *fd, int32_t cmd,
                    struct gf_flock *lock, dict_t *xdata);

int
aha_entrylk (call_frame_t *frame, xlator_t *this,
		   const char *volume, loc_t *loc, const char *basename,
		   entrylk_cmd cmd, entrylk_type type, dict_t *xdata);

int
aha_fentrylk (call_frame_t *frame, xlator_t *this,
		    const char *volume, fd_t *fd, const char *basename,
		    entrylk_cmd cmd, entrylk_type type, dict_t *xdata);
int
aha_readdir (call_frame_t *frame, xlator_t *this, fd_t *fd, size_t size,
                off_t off, dict_t *xdata);

int
aha_readdirp (call_frame_t *frame, xlator_t *this, fd_t *fd, size_t size,
                off_t off, dict_t *dict);

/* Callback functions */

int
aha_lookup_cbk (call_frame_t *frame, void *cookie, xlator_t *this,
		      int32_t op_ret, int32_t op_errno, inode_t *inode,
		      struct iatt *buf, dict_t *xdata, struct iatt *postparent);

int
aha_stat_cbk (call_frame_t *frame, void *cookie, xlator_t *this,
		    int32_t op_ret, int32_t op_errno, struct iatt *buf,
                    dict_t *xdata);

int
aha_setattr_cbk (call_frame_t *frame, void *cookie, xlator_t *this,
                       int32_t op_ret, int32_t op_errno,
                       struct iatt *preop, struct iatt *postop, dict_t *xdata);

int
aha_fsetattr_cbk (call_frame_t *frame, void *cookie, xlator_t *this,
                       int32_t op_ret, int32_t op_errno,
                       struct iatt *preop, struct iatt *postop, dict_t *xdata);

int
aha_truncate_cbk (call_frame_t *frame, void *cookie, xlator_t *this,
			int32_t op_ret, int32_t op_errno,
			struct iatt *prebuf, struct iatt *postbuf,
                        dict_t *xdata);


int
aha_ftruncate_cbk (call_frame_t *frame, void *cookie, xlator_t *this,
			 int32_t op_ret, int32_t op_errno, struct iatt *prebuf,
                         struct iatt *postbuf, dict_t *xdata);


int
aha_access_cbk (call_frame_t *frame, void *cookie, xlator_t *this,
		      int32_t op_ret, int32_t op_errno, dict_t *xdata);


int
aha_readlink_cbk (call_frame_t *frame, void *cookie, xlator_t *this,
			int32_t op_ret, int32_t op_errno,
			const char *path, struct iatt *sbuf, dict_t *xdata);


int
aha_mknod_cbk (call_frame_t *frame, void *cookie, xlator_t *this,
		     int32_t op_ret, int32_t op_errno, inode_t *inode,
                     struct iatt *buf, struct iatt *preparent,
                     struct iatt *postparent, dict_t *xdata);


int
aha_mkdir_cbk (call_frame_t *frame, void *cookie, xlator_t *this,
		     int32_t op_ret, int32_t op_errno, inode_t *inode,
                     struct iatt *buf, struct iatt *preparent,
                     struct iatt *postparent, dict_t *xdata);

int
aha_unlink_cbk (call_frame_t *frame, void *cookie, xlator_t *this,
		      int32_t op_ret, int32_t op_errno,
                      struct iatt *preparent, struct iatt *postparent,
                      dict_t *xdata);

int
aha_rmdir_cbk (call_frame_t *frame, void *cookie, xlator_t *this,
		     int32_t op_ret, int32_t op_errno,
                     struct iatt *preparent, struct iatt *postparent,
                     dict_t *xdata);
int
aha_symlink_cbk (call_frame_t *frame, void *cookie, xlator_t *this,
		       int32_t op_ret, int32_t op_errno, inode_t *inode,
                       struct iatt *buf, struct iatt *preparent,
                       struct iatt *postparent, dict_t *xdata);
int
aha_rename_cbk (call_frame_t *frame, void *cookie, xlator_t *this,
		      int32_t op_ret, int32_t op_errno, struct iatt *buf,
                      struct iatt *preoldparent, struct iatt *postoldparent,
                      struct iatt *prenewparent, struct iatt *postnewparent,
                      dict_t *xdata);

int
aha_link_cbk (call_frame_t *frame, void *cookie, xlator_t *this,
		    int32_t op_ret, int32_t op_errno, inode_t *inode,
                    struct iatt *buf, struct iatt *preparent,
                    struct iatt *postparent, dict_t *xdata);
int
aha_create_cbk (call_frame_t *frame, void *cookie, xlator_t *this,
		      int32_t op_ret, int32_t op_errno,
		      fd_t *fd, inode_t *inode, struct iatt *buf,
                      struct iatt *preparent, struct iatt *postparent,
                      dict_t *xdata);
int
aha_open_cbk (call_frame_t *frame, void *cookie, xlator_t *this,
		    int32_t op_ret, int32_t op_errno, fd_t *fd, dict_t *xdata);
int
aha_readv_cbk (call_frame_t *frame, void *cookie, xlator_t *this,
		     int32_t op_ret, int32_t op_errno,
		     struct iovec *vector, int32_t count,
		     struct iatt *stbuf, struct iobref *iobref, dict_t *xdata);

int
aha_writev_cbk (call_frame_t *frame, void *cookie, xlator_t *this,
		      int32_t op_ret, int32_t op_errno,
                      struct iatt *prebuf, struct iatt *postbuf, dict_t *xdata);
int
aha_flush_cbk (call_frame_t *frame, void *cookie, xlator_t *this,
		     int32_t op_ret, int32_t op_errno, dict_t *xdata);

int
aha_fsync_cbk (call_frame_t *frame, void *cookie,
		     xlator_t *this, int32_t op_ret,
		     int32_t op_errno, struct iatt *prebuf,
                     struct iatt *postbuf, dict_t *xdata);
int
aha_fstat_cbk (call_frame_t *frame, void *cookie, xlator_t *this,
                     int32_t op_ret, int32_t op_errno, struct iatt *buf,
                     dict_t *xdata);

int
aha_opendir_cbk (call_frame_t *frame, void *cookie, xlator_t *this,
		       int32_t op_ret, int32_t op_errno, fd_t *fd,
                       dict_t *xdata);
int
aha_fsyncdir_cbk (call_frame_t *frame, void *cookie, xlator_t *this,
			int32_t op_ret, int32_t op_errno, dict_t *xdata);
int
aha_statfs_cbk (call_frame_t *frame, void *cookie, xlator_t *this,
		      int32_t op_ret, int32_t op_errno, struct statvfs *buf,
                      dict_t *xdata);
int
aha_setxattr_cbk (call_frame_t *frame, void *cookie, xlator_t *this,
			int32_t op_ret, int32_t op_errno, dict_t *xdata);
int
aha_getxattr_cbk (call_frame_t *frame, void *cookie, xlator_t *this,
			int32_t op_ret, int32_t op_errno, dict_t *dict,
                        dict_t *xdata);

int
aha_fsetxattr_cbk (call_frame_t *frame, void *cookie, xlator_t *this,
                         int32_t op_ret, int32_t op_errno, dict_t *xdata);

int
aha_fgetxattr_cbk (call_frame_t *frame, void *cookie, xlator_t *this,
                         int32_t op_ret, int32_t op_errno, dict_t *dict,
                         dict_t *xdata);

int
aha_xattrop_cbk (call_frame_t *frame, void *cookie, xlator_t *this,
                       int32_t op_ret, int32_t op_errno, dict_t *dict,
                       dict_t *xdata);

int
aha_fxattrop_cbk (call_frame_t *frame, void *cookie, xlator_t *this,
			int32_t op_ret, int32_t op_errno, dict_t *dict,
                        dict_t *xdata);

int
aha_removexattr_cbk (call_frame_t *frame, void *cookie, xlator_t *this,
			   int32_t op_ret, int32_t op_errno, dict_t *xdata);
int
aha_fremovexattr_cbk (call_frame_t *frame, void *cookie, xlator_t *this,
			   int32_t op_ret, int32_t op_errno, dict_t *xdata);

int
aha_lk_cbk (call_frame_t *frame, void *cookie, xlator_t *this,
		  int32_t op_ret, int32_t op_errno, struct gf_flock *lock,
                  dict_t *xdata);

int
aha_inodelk_cbk (call_frame_t *frame, void *cookie, xlator_t *this,
                       int32_t op_ret, int32_t op_errno, dict_t *xdata);
int
aha_finodelk_cbk (call_frame_t *frame, void *cookie, xlator_t *this,
                        int32_t op_ret, int32_t op_errno, dict_t *xdata);

int
aha_entrylk_cbk (call_frame_t *frame, void *cookie, xlator_t *this,
                       int32_t op_ret, int32_t op_errno, dict_t *xdata);
int
aha_fentrylk_cbk (call_frame_t *frame, void *cookie, xlator_t *this,
                        int32_t op_ret, int32_t op_errno, dict_t *xdata);
int
aha_readdir_cbk (call_frame_t *frame, void *cookie, xlator_t *this,
		       int32_t op_ret, int32_t op_errno, gf_dirent_t *entries,
                       dict_t *xdata);
int
aha_readdirp_cbk (call_frame_t *frame, void *cookie, xlator_t *this,
                        int32_t op_ret, int32_t op_errno, gf_dirent_t *entries,
                        dict_t *xdata);

#endif /* _AHA_FOPS_H */
