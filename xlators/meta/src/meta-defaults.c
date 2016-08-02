/*
   Copyright (c) 2014 Red Hat, Inc. <http://www.redhat.com>
   This file is part of GlusterFS.

   This file is licensed to you under your choice of the GNU Lesser
   General Public License, version 3 or any later version (LGPLv3 or
   later), or the GNU General Public License, version 2 (GPLv2), in all
   cases as published by the Free Software Foundation.
*/

#include "xlator.h"
#include "defaults.h"

#include "meta-mem-types.h"
#include "meta.h"

#include "compat-errno.h"

int
meta_default_fgetxattr (call_frame_t *frame, xlator_t *this, fd_t *fd,
			const char *name, dict_t *xdata)
{
        return default_fgetxattr_failure_cbk (frame, EPERM);
}

int
meta_default_fsetxattr (call_frame_t *frame, xlator_t *this, fd_t *fd,
			dict_t *dict, int32_t flags, dict_t *xdata)
{
        return default_fsetxattr_failure_cbk (frame, EPERM);
}

int
meta_default_setxattr (call_frame_t *frame, xlator_t *this, loc_t *loc,
		       dict_t *dict, int32_t flags, dict_t *xdata)
{
        return default_setxattr_failure_cbk (frame, EPERM);
}

int
meta_default_statfs (call_frame_t *frame, xlator_t *this, loc_t *loc,
		     dict_t *xdata)
{
        return default_statfs_failure_cbk (frame, EPERM);
}

int
meta_default_fsyncdir (call_frame_t *frame, xlator_t *this, fd_t *fd,
		       int32_t flags, dict_t *xdata)
{
        return default_fsyncdir_failure_cbk (frame, EPERM);
}

int
meta_default_opendir (call_frame_t *frame, xlator_t *this, loc_t *loc,
		      fd_t *fd, dict_t *xdata)
{
	META_STACK_UNWIND (opendir, frame, 0, 0, fd, xdata);
	return 0;
}

int
meta_default_fstat (call_frame_t *frame, xlator_t *this, fd_t *fd,
		    dict_t *xdata)
{
	struct iatt iatt = { };

	meta_iatt_fill (&iatt, fd->inode, fd->inode->ia_type);

	META_STACK_UNWIND (fstat, frame, 0, 0, &iatt, xdata);

	return 0;
}

int
meta_default_fsync (call_frame_t *frame, xlator_t *this, fd_t *fd,
		    int32_t flags, dict_t *xdata)
{
        return default_fsync_failure_cbk (frame, EPERM);
}

int
meta_default_flush (call_frame_t *frame, xlator_t *this, fd_t *fd,
		    dict_t *xdata)
{
	META_STACK_UNWIND (flush, frame, 0, 0, xdata);
	return 0;
}

int
meta_default_writev (call_frame_t *frame, xlator_t *this, fd_t *fd,
		     struct iovec *vector, int32_t count, off_t off,
		     uint32_t flags, struct iobref *iobref, dict_t *xdata)
{
	struct meta_ops *ops = NULL;
	int ret = 0;
	struct iatt dummy = { };

	ops = meta_ops_get (fd->inode, this);
	if (!ops)
		goto err;

	if (!ops->file_write)
		goto err;

	ret = ops->file_write (this, fd, vector, count);

	META_STACK_UNWIND (writev, frame, (ret >= 0 ? ret : -1), (ret < 0 ? -ret : 0),
			   &dummy, &dummy, xdata);
	return 0;
err:
        return default_writev_failure_cbk (frame, EPERM);
}

int
meta_default_readv (call_frame_t *frame, xlator_t *this, fd_t *fd, size_t size,
		    off_t offset, uint32_t flags, dict_t *xdata)
{
	meta_fd_t *meta_fd = NULL;
	struct iovec iov = {};
	struct iobuf *iobuf = NULL;
	struct iobref *iobref = NULL;
	off_t copy_offset = 0;
	size_t copy_size = 0;
	struct iatt iatt = {};


	meta_fd = meta_fd_get (fd, this);
	if (!meta_fd)
		return default_readv_failure_cbk (frame, ENODATA);

	if (!meta_fd->size)
		meta_file_fill (this, fd);

	iobuf = iobuf_get2 (this->ctx->iobuf_pool, size);
	if (!iobuf)
		return default_readv_failure_cbk (frame, ENOMEM);

	iobref = iobref_new ();
	if (!iobref) {
		iobuf_unref (iobuf);
		return default_readv_failure_cbk (frame, ENOMEM);
	}

	if (iobref_add (iobref, iobuf) != 0) {
		iobref_unref (iobref);
		iobuf_unref (iobuf);
		return default_readv_failure_cbk (frame, ENOMEM);
	}

        /* iobref would have taken a ref */
        iobuf_unref (iobuf);

	iov.iov_base = iobuf_ptr (iobuf);

	copy_offset = min (meta_fd->size, offset);
	copy_size = min (size, (meta_fd->size - copy_offset));

	if (copy_size)
		memcpy (iov.iov_base, meta_fd->data + copy_offset, copy_size);
	iov.iov_len = copy_size;

	META_STACK_UNWIND (readv, frame, copy_size, 0, &iov, 1, &iatt, iobref, 0);

        iobref_unref (iobref);

	return 0;
}


int
meta_default_open (call_frame_t *frame, xlator_t *this, loc_t *loc,
		   int32_t flags, fd_t *fd, dict_t *xdata)
{
	dict_t *xdata_rsp = NULL;

	xdata_rsp = meta_direct_io_mode (xdata, frame);

	META_STACK_UNWIND (open, frame, 0, 0, fd, xdata_rsp);

	return 0;
}

int
meta_default_create (call_frame_t *frame, xlator_t *this, loc_t *loc,
		     int32_t flags, mode_t mode, mode_t umask, fd_t *fd,
		     dict_t *xdata)
{
        return default_create_failure_cbk (frame, EPERM);
}

int
meta_default_link (call_frame_t *frame, xlator_t *this, loc_t *oldloc,
		   loc_t *newloc, dict_t *xdata)
{
        return default_link_failure_cbk (frame, EPERM);
}

int
meta_default_rename (call_frame_t *frame, xlator_t *this, loc_t *oldloc,
		     loc_t *newloc, dict_t *xdata)
{
        return default_rename_failure_cbk (frame, EPERM);
}

int
meta_default_symlink (call_frame_t *frame, xlator_t *this, const char *linkpath,
		      loc_t *loc, mode_t umask, dict_t *xdata)
{
        return default_symlink_failure_cbk (frame, EPERM);
}

int
meta_default_rmdir (call_frame_t *frame, xlator_t *this, loc_t *loc, int flags,
		    dict_t *xdata)
{
        return default_rmdir_failure_cbk (frame, EPERM);
}

int
meta_default_unlink (call_frame_t *frame, xlator_t *this, loc_t *loc, int xflag,
		     dict_t *xdata)
{
        return default_unlink_failure_cbk (frame, EPERM);
}

int
meta_default_mkdir (call_frame_t *frame, xlator_t *this, loc_t *loc,
		    mode_t mode, mode_t umask, dict_t *xdata)
{
        return default_mkdir_failure_cbk (frame, EPERM);
}

int
meta_default_mknod (call_frame_t *frame, xlator_t *this, loc_t *loc,
		    mode_t mode, dev_t rdev, mode_t umask, dict_t *xdata)
{
        return default_mknod_failure_cbk (frame, EPERM);
}

int
meta_default_readlink (call_frame_t *frame, xlator_t *this, loc_t *loc,
		       size_t size, dict_t *xdata)
{
	struct meta_ops *ops = NULL;
	strfd_t *strfd = NULL;
	struct iatt iatt = { };

	ops = meta_ops_get (loc->inode, this);
	if (!ops->link_fill) {
		META_STACK_UNWIND (readlink, frame, -1, EPERM, 0, 0, 0);
		return 0;
	}

	strfd = strfd_open ();
	if (!strfd) {
		META_STACK_UNWIND (readlink, frame, -1, ENOMEM, 0, 0, 0);
		return 0;
	}

	ops->link_fill (this, loc->inode, strfd);

	meta_iatt_fill (&iatt, loc->inode, IA_IFLNK);

	if (strfd->data)
		META_STACK_UNWIND (readlink, frame, strlen (strfd->data), 0,
				   strfd->data, &iatt, xdata);
	else
		META_STACK_UNWIND (readlink, frame, -1, ENODATA, 0, 0, 0);

	strfd_close (strfd);

	return 0;
}

int
meta_default_access (call_frame_t *frame, xlator_t *this, loc_t *loc,
		     int32_t mask, dict_t *xdata)
{
        return default_access_failure_cbk (frame, EPERM);
}

int
meta_default_ftruncate (call_frame_t *frame, xlator_t *this, fd_t *fd,
			off_t offset, dict_t *xdata)
{
	struct iatt iatt = { };

	meta_iatt_fill (&iatt, fd->inode, IA_IFREG);

	META_STACK_UNWIND (ftruncate, frame, 0, 0, &iatt, &iatt, xdata);

        return 0;
}

int
meta_default_getxattr (call_frame_t *frame, xlator_t *this, loc_t *loc,
		       const char *name, dict_t *xdata)
{
        return default_getxattr_failure_cbk (frame, EPERM);
}

int
meta_default_xattrop (call_frame_t *frame, xlator_t *this, loc_t *loc,
		      gf_xattrop_flags_t flags, dict_t *dict, dict_t *xdata)
{
        return default_xattrop_failure_cbk (frame, EPERM);
}

int
meta_default_fxattrop (call_frame_t *frame, xlator_t *this, fd_t *fd,
		       gf_xattrop_flags_t flags, dict_t *dict, dict_t *xdata)
{
        return default_fxattrop_failure_cbk (frame, EPERM);
}

int
meta_default_removexattr (call_frame_t *frame, xlator_t *this, loc_t *loc,
			  const char *name, dict_t *xdata)
{
        return default_removexattr_failure_cbk (frame, EPERM);
}

int
meta_default_fremovexattr (call_frame_t *frame, xlator_t *this, fd_t *fd,
			   const char *name, dict_t *xdata)
{
        return default_fremovexattr_failure_cbk (frame, EPERM);
}

int
meta_default_lk (call_frame_t *frame, xlator_t *this, fd_t *fd,
		 int32_t cmd, struct gf_flock *lock, dict_t *xdata)
{
        return default_lk_failure_cbk (frame, EPERM);
}


int
meta_default_inodelk (call_frame_t *frame, xlator_t *this, const char *volume,
		      loc_t *loc, int32_t cmd, struct gf_flock *lock,
		      dict_t *xdata)
{
        return default_inodelk_failure_cbk (frame, EPERM);
}

int
meta_default_finodelk (call_frame_t *frame, xlator_t *this, const char *volume,
		       fd_t *fd, int32_t cmd, struct gf_flock *lock,
		       dict_t *xdata)
{
        return default_finodelk_failure_cbk (frame, EPERM);
}

int
meta_default_entrylk (call_frame_t *frame, xlator_t *this, const char *volume,
		      loc_t *loc, const char *basename, entrylk_cmd cmd,
		      entrylk_type type, dict_t *xdata)
{
        return default_entrylk_failure_cbk (frame, EPERM);
}

int
meta_default_fentrylk (call_frame_t *frame, xlator_t *this, const char *volume,
		       fd_t *fd, const char *basename, entrylk_cmd cmd,
		       entrylk_type type, dict_t *xdata)
{
        return default_fentrylk_failure_cbk (frame, EPERM);
}

int
meta_default_rchecksum (call_frame_t *frame, xlator_t *this, fd_t *fd,
			off_t offset, int32_t len, dict_t *xdata)
{
        return default_rchecksum_failure_cbk (frame, EPERM);
}


int
meta_default_readdir (call_frame_t *frame, xlator_t *this, fd_t *fd,
		      size_t size, off_t off, dict_t *xdata)
{
	meta_fd_t *meta_fd = NULL;
	int i = 0;
	gf_dirent_t head;
	gf_dirent_t *list = NULL;
	int ret = 0;
	int this_size = 0;
	int filled_size = 0;
	int fixed_size = 0;
	int dyn_size = 0;
	struct meta_dirent *fixed_dirents = NULL;
	struct meta_dirent *dyn_dirents = NULL;
	struct meta_dirent *dirents = NULL;
	struct meta_dirent *end = NULL;
	struct meta_ops *ops = NULL;

	INIT_LIST_HEAD (&head.list);

	ops = meta_ops_get (fd->inode, this);
	if (!ops)
		goto err;

	meta_fd = meta_fd_get (fd, this);
	if (!meta_fd)
		goto err;

	meta_dir_fill (this, fd);

	fixed_dirents = ops->fixed_dirents;
	fixed_size = fixed_dirents_len (fixed_dirents);

	dyn_dirents = meta_fd->dirents;
	dyn_size = meta_fd->size;

	for (i = off; i < (fixed_size + dyn_size);) {
		if (i >= fixed_size) {
			dirents = dyn_dirents + (i - fixed_size);
			end = dyn_dirents + dyn_size;
		} else {
			dirents = fixed_dirents + i;
			end = fixed_dirents + fixed_size;
		}

		while (dirents < end) {
			this_size = sizeof (gf_dirent_t) +
				strlen (dirents->name) + 1;
			if (this_size + filled_size > size)
				goto unwind;

			list = gf_dirent_for_name (dirents->name);
			if (!list)
				break;

			list->d_off = i + 1;
			list->d_ino = i + 42;
			switch (dirents->type) {
			case IA_IFDIR: list->d_type = DT_DIR; break;
			case IA_IFCHR: list->d_type = DT_CHR; break;
			case IA_IFBLK: list->d_type = DT_BLK; break;
			case IA_IFIFO: list->d_type = DT_FIFO; break;
			case IA_IFLNK: list->d_type = DT_LNK; break;
			case IA_IFREG: list->d_type = DT_REG; break;
			case IA_IFSOCK: list->d_type = DT_SOCK; break;
			case IA_INVAL: list->d_type = DT_UNKNOWN; break;
			}

			list_add_tail (&list->list, &head.list);
			ret++; i++; dirents++;
			filled_size += this_size;
		}
	}

unwind:
	META_STACK_UNWIND (readdir, frame, ret, 0, &head, xdata);

	gf_dirent_free (&head);

	return 0;
err:
	META_STACK_UNWIND (readdir, frame, -1, ENOMEM, 0, 0);
	return 0;
}


int
meta_default_readdirp (call_frame_t *frame, xlator_t *this, fd_t *fd,
		       size_t size, off_t off, dict_t *xdata)
{
        return meta_default_readdir (frame, this, fd, size, off, xdata);
}

int
meta_default_setattr (call_frame_t *frame, xlator_t *this, loc_t *loc,
		      struct iatt *stbuf, int32_t valid,
		      dict_t *xdata)
{
        return default_setattr_failure_cbk (frame, EPERM);
}

int
meta_default_truncate (call_frame_t *frame, xlator_t *this, loc_t *loc,
		       off_t offset, dict_t *xdata)
{
	struct iatt iatt = { };

	meta_iatt_fill (&iatt, loc->inode, IA_IFREG);

	META_STACK_UNWIND (truncate, frame, 0, 0, &iatt, &iatt, xdata);

        return 0;
}

int
meta_default_stat (call_frame_t *frame, xlator_t *this, loc_t *loc,
		   dict_t *xdata)
{
	struct iatt iatt = { };

	meta_iatt_fill (&iatt, loc->inode, loc->inode->ia_type);

	META_STACK_UNWIND (stat, frame, 0, 0, &iatt, xdata);

	return 0;
}

int
meta_default_lookup (call_frame_t *frame, xlator_t *this, loc_t *loc,
		     dict_t *xdata)
{
	struct meta_ops *ops = NULL;
	struct meta_dirent *dirent = NULL;
	struct meta_dirent *dp = NULL;
	int i = 0;
	int ret = 0;

	if (!loc->name)
		return meta_inode_discover (frame, this, loc, xdata);

	ops = meta_ops_get (loc->parent, this);
	if (!ops)
		return default_lookup_failure_cbk (frame, EPERM);

	for (dirent = ops->fixed_dirents; dirent && dirent->name; dirent++) {
		if (strcmp (dirent->name, loc->name) == 0)
			goto hook;
	}

	dirent = NULL;
	if (ops->dir_fill)
		ret = ops->dir_fill (this, loc->parent, &dp);

	for (i = 0; i < ret; i++) {
		if (strcmp (dp[i].name, loc->name) == 0) {
			dirent = &dp[i];
			goto hook;
		}
	}
hook:
	if (dirent && dirent->hook) {
		struct iatt parent = { };
		struct iatt iatt = { };

		dirent->hook (frame, this, loc, xdata);

		meta_iatt_fill (&iatt, loc->inode, dirent->type);

		META_STACK_UNWIND (lookup, frame, 0, 0, loc->inode, &iatt,
				   xdata, &parent);
	} else {
		META_STACK_UNWIND (lookup, frame, -1, ENOENT, 0, 0, 0, 0);
	}

	for (i = 0; i < ret; i++)
		GF_FREE ((void *)dp[i].name);
	GF_FREE (dp);

	return 0;
}

int
meta_default_fsetattr (call_frame_t *frame, xlator_t *this, fd_t *fd,
		       struct iatt *stbuf, int32_t valid, dict_t *xdata)
{
        return default_fsetattr_failure_cbk (frame, EPERM);
}

int
meta_default_fallocate (call_frame_t *frame, xlator_t *this, fd_t *fd,
			int32_t keep_size, off_t offset, size_t len,
			dict_t *xdata)
{
        return default_fallocate_failure_cbk (frame, EPERM);
}

int
meta_default_discard (call_frame_t *frame, xlator_t *this, fd_t *fd,
		      off_t offset, size_t len, dict_t *xdata)
{
        return default_discard_failure_cbk (frame, EPERM);
}

int
meta_default_zerofill (call_frame_t *frame, xlator_t *this, fd_t *fd,
		       off_t offset, off_t len, dict_t *xdata)
{
        return default_zerofill_failure_cbk (frame, EPERM);
}

#define SET_META_DEFAULT_FOP(f,name) do { if (!f->name) f->name = meta_default_##name ; } while (0)

struct xlator_fops *
meta_defaults_init (struct xlator_fops *fops)
{
        SET_META_DEFAULT_FOP (fops,create);
        SET_META_DEFAULT_FOP (fops,open);
        SET_META_DEFAULT_FOP (fops,stat);
        SET_META_DEFAULT_FOP (fops,readlink);
        SET_META_DEFAULT_FOP (fops,mknod);
        SET_META_DEFAULT_FOP (fops,mkdir);
        SET_META_DEFAULT_FOP (fops,unlink);
        SET_META_DEFAULT_FOP (fops,rmdir);
        SET_META_DEFAULT_FOP (fops,symlink);
        SET_META_DEFAULT_FOP (fops,rename);
        SET_META_DEFAULT_FOP (fops,link);
        SET_META_DEFAULT_FOP (fops,truncate);
        SET_META_DEFAULT_FOP (fops,readv);
        SET_META_DEFAULT_FOP (fops,writev);
        SET_META_DEFAULT_FOP (fops,statfs);
        SET_META_DEFAULT_FOP (fops,flush);
        SET_META_DEFAULT_FOP (fops,fsync);
        SET_META_DEFAULT_FOP (fops,setxattr);
        SET_META_DEFAULT_FOP (fops,getxattr);
        SET_META_DEFAULT_FOP (fops,fsetxattr);
        SET_META_DEFAULT_FOP (fops,fgetxattr);
        SET_META_DEFAULT_FOP (fops,removexattr);
        SET_META_DEFAULT_FOP (fops,fremovexattr);
        SET_META_DEFAULT_FOP (fops,opendir);
        SET_META_DEFAULT_FOP (fops,readdir);
        SET_META_DEFAULT_FOP (fops,readdirp);
        SET_META_DEFAULT_FOP (fops,fsyncdir);
        SET_META_DEFAULT_FOP (fops,access);
        SET_META_DEFAULT_FOP (fops,ftruncate);
        SET_META_DEFAULT_FOP (fops,fstat);
        SET_META_DEFAULT_FOP (fops,lk);
        SET_META_DEFAULT_FOP (fops,inodelk);
        SET_META_DEFAULT_FOP (fops,finodelk);
        SET_META_DEFAULT_FOP (fops,entrylk);
        SET_META_DEFAULT_FOP (fops,fentrylk);
        SET_META_DEFAULT_FOP (fops,lookup);
        SET_META_DEFAULT_FOP (fops,rchecksum);
        SET_META_DEFAULT_FOP (fops,xattrop);
        SET_META_DEFAULT_FOP (fops,fxattrop);
        SET_META_DEFAULT_FOP (fops,setattr);
        SET_META_DEFAULT_FOP (fops,fsetattr);
	SET_META_DEFAULT_FOP (fops,fallocate);
	SET_META_DEFAULT_FOP (fops,discard);
        SET_META_DEFAULT_FOP (fops,zerofill);

	return fops;
}
