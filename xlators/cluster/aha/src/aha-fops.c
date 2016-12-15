#include "aha-fops.h"

static void
__save_fop (struct aha_fop *fop, struct aha_conf *conf)
{
        list_add_tail (&fop->list, &conf->failed);
}

void
save_fop (struct aha_fop *fop, struct aha_conf *conf)
{
        LOCK (&conf->lock);
        {
                __save_fop (fop, conf);
        }
        UNLOCK (&conf->lock);
}

#define AHA_HANDLE_FOP(frame, type, cbk, obj, fn, args ...)             \
        do {                                                            \
                struct aha_fop *fop = aha_fop_new ();                        \
                if (!fop) {                                             \
                        gf_log (GF_AHA, GF_LOG_CRITICAL,                \
                                "Allocation failed, terminating "       \
                                "to prevent a hung mount.");            \
                        assert (0);                                     \
                }                                                       \
                fop->stub = fop_##type##_stub (frame, aha_##type,       \
                                                args);                  \
                fop->frame = frame;                                     \
                frame->local = fop;                                     \
                STACK_WIND (frame, cbk, obj, fn, args);                 \
        } while (0)                                                     \

/*
 * AHA_HANDLE_FOP_CBK
 *
 * 1) If the error returned is ENOTCONN *and* the timer that waits
 *    for the server to come back has not expired, store the fop to retry later.
 * 2) If the timer waiting for the server has expired, just unwind.
 * 3) If the error returned is something other than ENOTCONN, just unwind.
 *
 */
#define AHA_HANDLE_FOP_CBK(type, frame, args ...)                       \
        do {                                                            \
                struct aha_conf *conf = frame->this->private;                \
                struct aha_fop *fop = frame->local;                          \
                if (op_ret != 0 && op_errno == ENOTCONN &&              \
                        !aha_is_timer_expired (conf)) {                 \
                        gf_log (GF_AHA, GF_LOG_WARNING,                 \
                                "Got ENOTCONN from client, storing "    \
                                "to retry later!");                     \
                        save_fop (fop, conf);                           \
                } else {                                                \
                        AHA_DESTROY_LOCAL (frame);                      \
                        STACK_UNWIND_STRICT (type, frame, args);        \
                }                                                       \
        } while (0)                                                     \

int
aha_lookup_cbk (call_frame_t *frame, void *cookie, xlator_t *this,
		      int32_t op_ret, int32_t op_errno, inode_t *inode,
		      struct iatt *buf, dict_t *xdata, struct iatt *postparent)
{
        AHA_HANDLE_FOP_CBK (lookup, frame, op_ret, op_errno, inode,
                                buf, xdata, postparent);
        return 0;
}


int
aha_lookup (call_frame_t *frame, xlator_t *this, loc_t *loc,
                  dict_t *xdata)
{
        AHA_HANDLE_FOP (frame, lookup, aha_lookup_cbk,
                        FIRST_CHILD (this),
		        FIRST_CHILD (this)->fops->lookup,
		        loc, xdata);
        return 0;
}


int
aha_stat_cbk (call_frame_t *frame, void *cookie, xlator_t *this,
		    int32_t op_ret, int32_t op_errno, struct iatt *buf,
                    dict_t *xdata)
{
	AHA_HANDLE_FOP_CBK (stat, frame, op_ret, op_errno, buf, xdata);
        return 0;
}

int
aha_stat (call_frame_t *frame, xlator_t *this, loc_t *loc, dict_t *xdata)
{
	AHA_HANDLE_FOP (frame, stat, aha_stat_cbk,
		        FIRST_CHILD (this),
		        FIRST_CHILD (this)->fops->stat,
		        loc, xdata);
        return 0;
}


int
aha_setattr_cbk (call_frame_t *frame, void *cookie, xlator_t *this,
                       int32_t op_ret, int32_t op_errno,
                       struct iatt *preop, struct iatt *postop,
                       dict_t *xdata)
{
	AHA_HANDLE_FOP_CBK (setattr, frame, op_ret, op_errno, preop,
                            postop, xdata);
        return 0;
}


int
aha_setattr (call_frame_t *frame, xlator_t *this, loc_t *loc,
                   struct iatt *stbuf, int32_t valid, dict_t *xdata)
{
	AHA_HANDLE_FOP (frame, setattr, aha_setattr_cbk,
		        FIRST_CHILD (this),
		        FIRST_CHILD (this)->fops->setattr,
		        loc, stbuf, valid, xdata);
        return 0;
}


int
aha_fsetattr_cbk (call_frame_t *frame, void *cookie, xlator_t *this,
                       int32_t op_ret, int32_t op_errno,
                       struct iatt *preop, struct iatt *postop, dict_t *xdata)
{
	AHA_HANDLE_FOP_CBK (fsetattr, frame, op_ret, op_errno, preop,
                            postop, xdata);
        return 0;
}

int
aha_fsetattr (call_frame_t *frame, xlator_t *this, fd_t *fd,
                    struct iatt *stbuf, int32_t valid, dict_t *xdata)
{
	AHA_HANDLE_FOP (frame, fsetattr, aha_fsetattr_cbk,
		        FIRST_CHILD (this),
		        FIRST_CHILD (this)->fops->fsetattr,
		        fd, stbuf, valid, xdata);
        return 0;
}


int
aha_truncate_cbk (call_frame_t *frame, void *cookie, xlator_t *this,
			int32_t op_ret, int32_t op_errno,
			struct iatt *prebuf, struct iatt *postbuf,
                        dict_t *xdata)
{
	AHA_HANDLE_FOP_CBK (truncate, frame, op_ret, op_errno,
                             prebuf, postbuf, xdata);
        return 0;
}


int
aha_truncate (call_frame_t *frame, xlator_t *this, loc_t *loc,
                    off_t offset, dict_t *xdata)
{
	AHA_HANDLE_FOP (frame, truncate, aha_truncate_cbk,
		    FIRST_CHILD (this),
		    FIRST_CHILD (this)->fops->truncate,
		    loc, offset, xdata);
        return 0;
}


int
aha_ftruncate_cbk (call_frame_t *frame, void *cookie, xlator_t *this,
			 int32_t op_ret, int32_t op_errno, struct iatt *prebuf,
                         struct iatt *postbuf, dict_t *xdata)
{
	AHA_HANDLE_FOP_CBK (ftruncate, frame, op_ret, op_errno,
                             prebuf, postbuf, xdata);
        return 0;
}


int
aha_ftruncate (call_frame_t *frame, xlator_t *this, fd_t *fd,
		     off_t offset, dict_t *xdata)
{
	AHA_HANDLE_FOP (frame, ftruncate, aha_ftruncate_cbk,
		    FIRST_CHILD (this),
		    FIRST_CHILD (this)->fops->ftruncate,
		    fd, offset, xdata);
        return 0;
}


int
aha_access_cbk (call_frame_t *frame, void *cookie, xlator_t *this,
		      int32_t op_ret, int32_t op_errno, dict_t *xdata)
{
	AHA_HANDLE_FOP_CBK (access, frame, op_ret, op_errno, xdata);
        return 0;
}


int
aha_access (call_frame_t *frame, xlator_t *this, loc_t *loc,
		  int32_t mask, dict_t *xdata)
{
	AHA_HANDLE_FOP (frame, access, aha_access_cbk,
		    FIRST_CHILD (this),
		    FIRST_CHILD (this)->fops->access,
		    loc, mask, xdata);
        return 0;
}


int
aha_readlink_cbk (call_frame_t *frame, void *cookie, xlator_t *this,
			int32_t op_ret, int32_t op_errno,
			const char *path, struct iatt *sbuf, dict_t *xdata)
{
	AHA_HANDLE_FOP_CBK (readlink, frame, op_ret, op_errno,
                            path, sbuf, xdata);
        return 0;
}


int
aha_readlink (call_frame_t *frame, xlator_t *this, loc_t *loc,
                    size_t size, dict_t *xdata)
{
	AHA_HANDLE_FOP (frame, readlink, aha_readlink_cbk,
		    FIRST_CHILD (this),
		    FIRST_CHILD (this)->fops->readlink,
		    loc, size, xdata);
        return 0;
}


int
aha_mknod_cbk (call_frame_t *frame, void *cookie, xlator_t *this,
		     int32_t op_ret, int32_t op_errno, inode_t *inode,
                     struct iatt *buf, struct iatt *preparent,
                     struct iatt *postparent, dict_t *xdata)
{
	AHA_HANDLE_FOP_CBK (mknod, frame, op_ret, op_errno,
                             inode, buf,
                             preparent, postparent, xdata);
        return 0;
}


int
aha_mknod (call_frame_t *frame, xlator_t *this, loc_t *loc,
		 mode_t mode, dev_t rdev, mode_t umask, dict_t *xdata)
{
	AHA_HANDLE_FOP (frame, mknod, aha_mknod_cbk,
		    FIRST_CHILD (this),
		    FIRST_CHILD (this)->fops->mknod,
		    loc, mode, rdev, umask, xdata);
        return 0;
}


int
aha_mkdir_cbk (call_frame_t *frame, void *cookie, xlator_t *this,
		     int32_t op_ret, int32_t op_errno, inode_t *inode,
                     struct iatt *buf, struct iatt *preparent,
                     struct iatt *postparent, dict_t *xdata)
{
	AHA_HANDLE_FOP_CBK (mkdir, frame, op_ret, op_errno,
                             inode, buf,
                             preparent, postparent, xdata);
        return 0;
}

int
aha_mkdir (call_frame_t *frame, xlator_t *this,
		 loc_t *loc, mode_t mode, mode_t umask, dict_t *xdata)
{
	AHA_HANDLE_FOP (frame, mkdir, aha_mkdir_cbk,
		    FIRST_CHILD (this),
		    FIRST_CHILD (this)->fops->mkdir,
		    loc, mode, umask, xdata);
        return 0;
}


int
aha_unlink_cbk (call_frame_t *frame, void *cookie, xlator_t *this,
		      int32_t op_ret, int32_t op_errno,
                      struct iatt *preparent, struct iatt *postparent,
                      dict_t *xdata)
{
	AHA_HANDLE_FOP_CBK (unlink, frame, op_ret, op_errno,
                             preparent, postparent, xdata);
        return 0;
}


int
aha_unlink (call_frame_t *frame, xlator_t *this, loc_t *loc, int xflag,
                  dict_t *xdata)
{
	AHA_HANDLE_FOP (frame, unlink, aha_unlink_cbk,
		    FIRST_CHILD (this),
		    FIRST_CHILD (this)->fops->unlink,
		    loc, xflag, xdata);
        return 0;
}


int
aha_rmdir_cbk (call_frame_t *frame, void *cookie, xlator_t *this,
		     int32_t op_ret, int32_t op_errno,
                     struct iatt *preparent, struct iatt *postparent,
                     dict_t *xdata)
{
	AHA_HANDLE_FOP_CBK (rmdir, frame, op_ret, op_errno,
                             preparent, postparent, xdata);
        return 0;
}


int
aha_rmdir (call_frame_t *frame, xlator_t *this, loc_t *loc, int flags,
                 dict_t *xdata)
{
	AHA_HANDLE_FOP (frame, rmdir, aha_rmdir_cbk,
		    FIRST_CHILD (this),
		    FIRST_CHILD (this)->fops->rmdir,
		    loc, flags, xdata);
        return 0;
}


int
aha_symlink_cbk (call_frame_t *frame, void *cookie, xlator_t *this,
		       int32_t op_ret, int32_t op_errno, inode_t *inode,
                       struct iatt *buf, struct iatt *preparent,
                       struct iatt *postparent, dict_t *xdata)
{
	AHA_HANDLE_FOP_CBK (symlink, frame, op_ret, op_errno, inode, buf,
                             preparent, postparent, xdata);
        return 0;
}


int
aha_symlink (call_frame_t *frame, xlator_t *this, const char *linkpath,
		   loc_t *loc, mode_t umask, dict_t *xdata)
{
	AHA_HANDLE_FOP (frame, symlink, aha_symlink_cbk,
		    FIRST_CHILD (this),
		    FIRST_CHILD (this)->fops->symlink,
		    linkpath, loc, umask, xdata);
        return 0;
}


int
aha_rename_cbk (call_frame_t *frame, void *cookie, xlator_t *this,
		      int32_t op_ret, int32_t op_errno, struct iatt *buf,
                      struct iatt *preoldparent, struct iatt *postoldparent,
                      struct iatt *prenewparent, struct iatt *postnewparent,
                      dict_t *xdata)
{
	AHA_HANDLE_FOP_CBK (rename, frame, op_ret, op_errno, buf,
                             preoldparent, postoldparent,
                             prenewparent, postnewparent, xdata);
        return 0;
}


int
aha_rename (call_frame_t *frame, xlator_t *this,
		  loc_t *oldloc, loc_t *newloc, dict_t *xdata)
{
	AHA_HANDLE_FOP (frame, rename, aha_rename_cbk,
		    FIRST_CHILD (this),
		    FIRST_CHILD (this)->fops->rename,
		    oldloc, newloc, xdata);
        return 0;
}


int
aha_link_cbk (call_frame_t *frame, void *cookie, xlator_t *this,
		    int32_t op_ret, int32_t op_errno, inode_t *inode,
                    struct iatt *buf, struct iatt *preparent,
                    struct iatt *postparent, dict_t *xdata)
{
	AHA_HANDLE_FOP_CBK (link, frame, op_ret, op_errno, inode, buf,
                             preparent, postparent, xdata);
        return 0;
}


int
aha_link (call_frame_t *frame, xlator_t *this,
		loc_t *oldloc, loc_t *newloc, dict_t *xdata)
{
	AHA_HANDLE_FOP (frame, link, aha_link_cbk,
		    FIRST_CHILD (this),
		    FIRST_CHILD (this)->fops->link,
		    oldloc, newloc, xdata);
        return 0;
}


int
aha_create_cbk (call_frame_t *frame, void *cookie, xlator_t *this,
		      int32_t op_ret, int32_t op_errno,
		      fd_t *fd, inode_t *inode, struct iatt *buf,
                      struct iatt *preparent, struct iatt *postparent,
                      dict_t *xdata)
{
	AHA_HANDLE_FOP_CBK (create, frame, op_ret, op_errno, fd, inode, buf,
                             preparent, postparent, xdata);
        return 0;
}


int
aha_create (call_frame_t *frame, xlator_t *this, loc_t *loc,
		  int32_t flags, mode_t mode, mode_t umask, fd_t *fd,
                  dict_t *xdata)
{
	AHA_HANDLE_FOP (frame, create, aha_create_cbk,
		    FIRST_CHILD (this),
		    FIRST_CHILD (this)->fops->create,
		    loc, flags, mode, umask, fd, xdata);
        return 0;
}


int
aha_open_cbk (call_frame_t *frame, void *cookie, xlator_t *this,
		    int32_t op_ret, int32_t op_errno, fd_t *fd, dict_t *xdata)
{
	AHA_HANDLE_FOP_CBK (open, frame, op_ret, op_errno, fd, xdata);
        return 0;
}


int
aha_open (call_frame_t *frame, xlator_t *this, loc_t *loc,
		int32_t flags, fd_t *fd, dict_t *xdata)
{
	AHA_HANDLE_FOP (frame, open, aha_open_cbk,
		    FIRST_CHILD (this),
		    FIRST_CHILD (this)->fops->open,
		    loc, flags, fd, xdata);
        return 0;
}

int
aha_readv_cbk (call_frame_t *frame, void *cookie, xlator_t *this,
		     int32_t op_ret, int32_t op_errno,
		     struct iovec *vector, int32_t count,
		     struct iatt *stbuf, struct iobref *iobref, dict_t *xdata)
{
	AHA_HANDLE_FOP_CBK (readv, frame, op_ret, op_errno,
                             vector, count, stbuf, iobref, xdata);
        return 0;
}

int
aha_readv (call_frame_t *frame, xlator_t *this,
		 fd_t *fd, size_t size, off_t offset, uint32_t flags,
                 dict_t *xdata)
{
        AHA_HANDLE_FOP (frame, readv, aha_readv_cbk,
                        FIRST_CHILD (this), FIRST_CHILD (this)->fops->readv,
                        fd, size, offset, flags, xdata);
        return 0;
}


int
aha_writev_cbk (call_frame_t *frame, void *cookie, xlator_t *this,
		      int32_t op_ret, int32_t op_errno,
                      struct iatt *prebuf, struct iatt *postbuf, dict_t *xdata)
{
        AHA_HANDLE_FOP_CBK (writev, frame, op_ret, op_errno,
                            prebuf, postbuf, xdata);
        return 0;
}

int
aha_writev (call_frame_t *frame, xlator_t *this, fd_t *fd,
		  struct iovec *vector, int32_t count,
		  off_t off, uint32_t flags, struct iobref *iobref,
                  dict_t *xdata)
{
        AHA_HANDLE_FOP (frame, writev, aha_writev_cbk,
                        FIRST_CHILD (this), FIRST_CHILD (this)->fops->writev,
                        fd, vector, count, off, flags, iobref, xdata);
        return 0;
}


int
aha_flush_cbk (call_frame_t *frame, void *cookie, xlator_t *this,
		     int32_t op_ret, int32_t op_errno, dict_t *xdata)
{
	AHA_HANDLE_FOP_CBK (flush, frame, op_ret, op_errno, xdata);
        return 0;
}


int
aha_flush (call_frame_t *frame, xlator_t *this, fd_t *fd, dict_t *xdata)
{
	AHA_HANDLE_FOP (frame, flush, aha_flush_cbk,
                    FIRST_CHILD (this),
		    FIRST_CHILD (this)->fops->flush,
		    fd, xdata);
        return 0;
}


int
aha_fsync_cbk (call_frame_t *frame, void *cookie,
		     xlator_t *this, int32_t op_ret,
		     int32_t op_errno, struct iatt *prebuf,
                     struct iatt *postbuf, dict_t *xdata)
{
	AHA_HANDLE_FOP_CBK (fsync, frame, op_ret, op_errno,
                            prebuf, postbuf, xdata);
        return 0;
}


int
aha_fsync (call_frame_t *frame, xlator_t *this, fd_t *fd, int32_t flags,
           dict_t *xdata)
{
	AHA_HANDLE_FOP (frame, fsync, aha_fsync_cbk,
		    FIRST_CHILD (this),
		    FIRST_CHILD (this)->fops->fsync,
		    fd, flags, xdata);
        return 0;
}


int
aha_fstat_cbk (call_frame_t *frame, void *cookie, xlator_t *this,
               int32_t op_ret, int32_t op_errno, struct iatt *buf,
               dict_t *xdata)
{
	AHA_HANDLE_FOP_CBK (fstat, frame, op_ret, op_errno, buf, xdata);
        return 0;
}


int
aha_fstat (call_frame_t *frame, xlator_t *this, fd_t *fd, dict_t *xdata)
{
	AHA_HANDLE_FOP (frame, fstat, aha_fstat_cbk,
		    FIRST_CHILD (this),
		    FIRST_CHILD (this)->fops->fstat,
		    fd, xdata);
        return 0;
}


int
aha_opendir_cbk (call_frame_t *frame, void *cookie, xlator_t *this,
		       int32_t op_ret, int32_t op_errno, fd_t *fd,
                       dict_t *xdata)
{
	AHA_HANDLE_FOP_CBK (opendir, frame, op_ret, op_errno, fd, xdata);
        return 0;
}


int
aha_opendir (call_frame_t *frame, xlator_t *this, loc_t *loc, fd_t *fd,
             dict_t *xdata)
{
	AHA_HANDLE_FOP (frame, opendir, aha_opendir_cbk,
		    FIRST_CHILD (this),
		    FIRST_CHILD (this)->fops->opendir,
		    loc, fd, xdata);
        return 0;
}

int
aha_fsyncdir_cbk (call_frame_t *frame, void *cookie, xlator_t *this,
			int32_t op_ret, int32_t op_errno, dict_t *xdata)
{
	AHA_HANDLE_FOP_CBK (fsyncdir, frame, op_ret, op_errno, xdata);
        return 0;
}


int
aha_fsyncdir (call_frame_t *frame, xlator_t *this, fd_t *fd,
                    int32_t flags, dict_t *xdata)
{
	AHA_HANDLE_FOP (frame, fsyncdir, aha_fsyncdir_cbk,
		    FIRST_CHILD (this),
		    FIRST_CHILD (this)->fops->fsyncdir,
		    fd, flags, xdata);
        return 0;
}


int
aha_statfs_cbk (call_frame_t *frame, void *cookie, xlator_t *this,
		int32_t op_ret, int32_t op_errno, struct statvfs *buf,
                dict_t *xdata)
{
	AHA_HANDLE_FOP_CBK (statfs, frame, op_ret, op_errno, buf, xdata);
        return 0;
}


int
aha_statfs (call_frame_t *frame, xlator_t *this, loc_t *loc, dict_t *xdata)
{
	AHA_HANDLE_FOP (frame, statfs, aha_statfs_cbk,
		    FIRST_CHILD (this),
		    FIRST_CHILD (this)->fops->statfs,
		    loc, xdata);
        return 0;
}



int
aha_setxattr_cbk (call_frame_t *frame, void *cookie, xlator_t *this,
			int32_t op_ret, int32_t op_errno, dict_t *xdata)
{
	AHA_HANDLE_FOP_CBK (setxattr, frame, op_ret, op_errno, xdata);
        return 0;
}


int
aha_setxattr (call_frame_t *frame, xlator_t *this, loc_t *loc,
		    dict_t *dict, int32_t flags, dict_t *xdata)
{
	AHA_HANDLE_FOP (frame, setxattr, aha_setxattr_cbk,
		    FIRST_CHILD (this),
		    FIRST_CHILD (this)->fops->setxattr,
		    loc, dict, flags, xdata);
        return 0;
}


int
aha_getxattr_cbk (call_frame_t *frame, void *cookie, xlator_t *this,
		  int32_t op_ret, int32_t op_errno, dict_t *dict,
                  dict_t *xdata)
{
	AHA_HANDLE_FOP_CBK (getxattr, frame, op_ret, op_errno, dict, xdata);
        return 0;
}


int
aha_getxattr (call_frame_t *frame, xlator_t *this, loc_t *loc,
		    const char *name, dict_t *xdata)
{
	AHA_HANDLE_FOP (frame, getxattr, aha_getxattr_cbk,
		    FIRST_CHILD (this),
		    FIRST_CHILD (this)->fops->getxattr,
		    loc, name, xdata);
        return 0;
}

int
aha_fsetxattr_cbk (call_frame_t *frame, void *cookie, xlator_t *this,
                         int32_t op_ret, int32_t op_errno, dict_t *xdata)
{
	AHA_HANDLE_FOP_CBK (fsetxattr, frame, op_ret, op_errno, xdata);
        return 0;
}


int
aha_fsetxattr (call_frame_t *frame, xlator_t *this, fd_t *fd,
                     dict_t *dict, int32_t flags, dict_t *xdata)
{
	AHA_HANDLE_FOP (frame, fsetxattr, aha_fsetxattr_cbk,
		    FIRST_CHILD (this),
		    FIRST_CHILD (this)->fops->fsetxattr,
		    fd, dict, flags, xdata);
        return 0;
}


int
aha_fgetxattr_cbk (call_frame_t *frame, void *cookie, xlator_t *this,
                   int32_t op_ret, int32_t op_errno, dict_t *dict,
                   dict_t *xdata)
{
	AHA_HANDLE_FOP_CBK (fgetxattr, frame, op_ret, op_errno, dict, xdata);
        return 0;
}


int
aha_fgetxattr (call_frame_t *frame, xlator_t *this, fd_t *fd,
                     const char *name, dict_t *xdata)
{
	AHA_HANDLE_FOP (frame, fgetxattr, aha_fgetxattr_cbk,
		    FIRST_CHILD (this),
		    FIRST_CHILD (this)->fops->fgetxattr,
		    fd, name, xdata);
        return 0;
}


int
aha_xattrop_cbk (call_frame_t *frame, void *cookie, xlator_t *this,
                 int32_t op_ret, int32_t op_errno, dict_t *dict,
                 dict_t *xdata)
{
	AHA_HANDLE_FOP_CBK (xattrop, frame, op_ret, op_errno, dict, xdata);
        return 0;
}


int
aha_xattrop (call_frame_t *frame, xlator_t *this, loc_t *loc,
		   gf_xattrop_flags_t flags, dict_t *dict, dict_t *xdata)
{
	AHA_HANDLE_FOP (frame, xattrop, aha_xattrop_cbk,
		    FIRST_CHILD (this),
		    FIRST_CHILD (this)->fops->xattrop,
		    loc, flags, dict, xdata);
        return 0;
}


int
aha_fxattrop_cbk (call_frame_t *frame, void *cookie, xlator_t *this,
		  int32_t op_ret, int32_t op_errno, dict_t *dict,
                  dict_t *xdata)
{
	AHA_HANDLE_FOP_CBK (fxattrop, frame, op_ret, op_errno, dict, xdata);
        return 0;
}


int
aha_fxattrop (call_frame_t *frame, xlator_t *this, fd_t *fd,
		    gf_xattrop_flags_t flags, dict_t *dict, dict_t *xdata)
{
	AHA_HANDLE_FOP (frame, fxattrop, aha_fxattrop_cbk,
		    FIRST_CHILD (this),
		    FIRST_CHILD (this)->fops->fxattrop,
		    fd, flags, dict, xdata);
        return 0;
}


int
aha_removexattr_cbk (call_frame_t *frame, void *cookie, xlator_t *this,
			   int32_t op_ret, int32_t op_errno, dict_t *xdata)
{
	AHA_HANDLE_FOP_CBK (removexattr, frame, op_ret, op_errno, xdata);
        return 0;
}


int
aha_removexattr (call_frame_t *frame, xlator_t *this, loc_t *loc,
		       const char *name, dict_t *xdata)
{
	AHA_HANDLE_FOP (frame, removexattr, aha_removexattr_cbk,
		    FIRST_CHILD (this),
		    FIRST_CHILD (this)->fops->removexattr,
		    loc, name, xdata);
        return 0;
}

int
aha_fremovexattr_cbk (call_frame_t *frame, void *cookie, xlator_t *this,
			   int32_t op_ret, int32_t op_errno, dict_t *xdata)
{
	AHA_HANDLE_FOP_CBK (fremovexattr, frame, op_ret, op_errno, xdata);
        return 0;
}


int
aha_fremovexattr (call_frame_t *frame, xlator_t *this, fd_t *fd,
                        const char *name, dict_t *xdata)
{
	AHA_HANDLE_FOP (frame, fremovexattr, aha_fremovexattr_cbk,
		    FIRST_CHILD (this),
		    FIRST_CHILD (this)->fops->fremovexattr,
		    fd, name, xdata);
        return 0;
}


int
aha_lk_cbk (call_frame_t *frame, void *cookie, xlator_t *this,
            int32_t op_ret, int32_t op_errno, struct gf_flock *lock,
            dict_t *xdata)
{
	AHA_HANDLE_FOP_CBK (lk, frame, op_ret, op_errno, lock, xdata);
        return 0;
}


int
aha_lk (call_frame_t *frame, xlator_t *this, fd_t *fd, int32_t cmd,
	      struct gf_flock *lock, dict_t *xdata)
{
	AHA_HANDLE_FOP (frame, lk, aha_lk_cbk,
		    FIRST_CHILD (this),
		    FIRST_CHILD (this)->fops->lk,
		    fd, cmd, lock, xdata);
        return 0;
}


int
aha_inodelk_cbk (call_frame_t *frame, void *cookie, xlator_t *this,
                       int32_t op_ret, int32_t op_errno, dict_t *xdata)
{
	AHA_HANDLE_FOP_CBK (inodelk, frame, op_ret, op_errno, xdata);
        return 0;
}


int
aha_inodelk (call_frame_t *frame, xlator_t *this,
		   const char *volume, loc_t *loc, int32_t cmd,
                   struct gf_flock *lock, dict_t *xdata)
{
	AHA_HANDLE_FOP (frame, inodelk, aha_inodelk_cbk,
		    FIRST_CHILD (this),
		    FIRST_CHILD (this)->fops->inodelk,
		    volume, loc, cmd, lock, xdata);
        return 0;
}


int
aha_finodelk_cbk (call_frame_t *frame, void *cookie, xlator_t *this,
                        int32_t op_ret, int32_t op_errno, dict_t *xdata)
{
	AHA_HANDLE_FOP_CBK (finodelk, frame, op_ret, op_errno, xdata);
        return 0;
}


int
aha_finodelk (call_frame_t *frame, xlator_t *this,
		    const char *volume, fd_t *fd, int32_t cmd,
                    struct gf_flock *lock, dict_t *xdata)
{
	AHA_HANDLE_FOP (frame, finodelk, aha_finodelk_cbk,
		    FIRST_CHILD (this),
		    FIRST_CHILD (this)->fops->finodelk,
		    volume, fd, cmd, lock, xdata);
        return 0;
}


int
aha_entrylk_cbk (call_frame_t *frame, void *cookie, xlator_t *this,
                       int32_t op_ret, int32_t op_errno, dict_t *xdata)
{
	AHA_HANDLE_FOP_CBK (entrylk, frame, op_ret, op_errno, xdata);
        return 0;
}


int
aha_entrylk (call_frame_t *frame, xlator_t *this,
		   const char *volume, loc_t *loc, const char *basename,
		   entrylk_cmd cmd, entrylk_type type, dict_t *xdata)
{
	AHA_HANDLE_FOP (frame, entrylk, aha_entrylk_cbk,
		    FIRST_CHILD (this),
		    FIRST_CHILD (this)->fops->entrylk,
		    volume, loc, basename, cmd, type, xdata);
        return 0;
}


int
aha_fentrylk_cbk (call_frame_t *frame, void *cookie, xlator_t *this,
                        int32_t op_ret, int32_t op_errno, dict_t *xdata)
{
	AHA_HANDLE_FOP_CBK (fentrylk, frame, op_ret, op_errno, xdata);
        return 0;
}


int
aha_fentrylk (call_frame_t *frame, xlator_t *this,
		    const char *volume, fd_t *fd, const char *basename,
		    entrylk_cmd cmd, entrylk_type type, dict_t *xdata)
{
	AHA_HANDLE_FOP (frame, fentrylk, aha_fentrylk_cbk,
		    FIRST_CHILD (this),
		    FIRST_CHILD (this)->fops->fentrylk,
		    volume, fd, basename, cmd, type, xdata);
        return 0;
}

int
aha_readdir_cbk (call_frame_t *frame, void *cookie, xlator_t *this,
		       int32_t op_ret, int32_t op_errno, gf_dirent_t *entries,
                       dict_t *xdata)
{
	AHA_HANDLE_FOP_CBK (readdir, frame, op_ret, op_errno, entries, xdata);
	return 0;
}


int
aha_readdir (call_frame_t *frame, xlator_t *this, fd_t *fd,
		   size_t size, off_t off, dict_t *xdata)
{
	AHA_HANDLE_FOP (frame, readdir, aha_readdir_cbk,
		    FIRST_CHILD (this),
		    FIRST_CHILD (this)->fops->readdir,
		    fd, size, off, xdata);
	return 0;
}


int
aha_readdirp_cbk (call_frame_t *frame, void *cookie, xlator_t *this,
                        int32_t op_ret, int32_t op_errno, gf_dirent_t *entries,
                        dict_t *xdata)
{
	AHA_HANDLE_FOP_CBK (readdirp, frame, op_ret, op_errno, entries, xdata);
	return 0;
}


int
aha_readdirp (call_frame_t *frame, xlator_t *this, fd_t *fd, size_t size,
                    off_t off, dict_t *dict)
{
	AHA_HANDLE_FOP (frame, readdirp, aha_readdirp_cbk,
                    FIRST_CHILD (this),
                    FIRST_CHILD (this)->fops->readdirp,
                    fd, size, off, dict);
	return 0;
}
