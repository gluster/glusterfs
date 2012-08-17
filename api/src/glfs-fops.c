/*
  Copyright (c) 2012 Red Hat, Inc. <http://www.redhat.com>
  This file is part of GlusterFS.

  This file is licensed to you under your choice of the GNU Lesser
  General Public License, version 3 or any later version (LGPLv3 or
  later), or the GNU General Public License, version 2 (GPLv2), in all
  cases as published by the Free Software Foundation.
*/


#include "glfs-internal.h"
#include "glfs-mem-types.h"
#include "syncop.h"
#include "glfs.h"


struct glfs_fd *
glfs_open (struct glfs *fs, const char *path, int flags)
{
	int              ret = -1;
	struct glfs_fd  *glfd = NULL;
	xlator_t        *subvol = NULL;
	loc_t            loc = {0, };
	struct iatt      iatt = {0, };

	__glfs_entry_fs (fs);

	subvol = glfs_active_subvol (fs);
	if (!subvol) {
		ret = -1;
		errno = EIO;
		goto out;
	}

	glfd = GF_CALLOC (1, sizeof (*glfd), glfs_mt_glfs_fd_t);
	if (!glfd)
		goto out;

	ret = glfs_resolve (fs, subvol, path, &loc, &iatt);
	if (ret)
		goto out;

	if (IA_ISDIR (iatt.ia_type)) {
		ret = -1;
		errno = EISDIR;
		goto out;
	}

	if (!IA_ISREG (iatt.ia_type)) {
		ret = -1;
		errno = EINVAL;
		goto out;
	}

	glfd->fd = fd_create (loc.inode, getpid());
	if (!glfd->fd) {
		ret = -1;
		errno = ENOMEM;
		goto out;
	}

	ret = syncop_open (subvol, &loc, flags, glfd->fd);
out:
	loc_wipe (&loc);

	if (ret && glfd) {
		glfs_fd_destroy (glfd);
		glfd = NULL;
	}

	return glfd;
}


int
glfs_close (struct glfs_fd *glfd)
{
	xlator_t  *subvol = NULL;
	int        ret = -1;

	__glfs_entry_fd (glfd);

	subvol = glfs_fd_subvol (glfd);

	ret = syncop_flush (subvol, glfd->fd);

	glfs_fd_destroy (glfd);

	return ret;
}


int
glfs_lstat (struct glfs *fs, const char *path, struct stat *stat)
{
	int              ret = -1;
	xlator_t        *subvol = NULL;
	loc_t            loc = {0, };
	struct iatt      iatt = {0, };

	__glfs_entry_fs (fs);

	subvol = glfs_active_subvol (fs);
	if (!subvol) {
		ret = -1;
		errno = EIO;
		goto out;
	}

	ret = glfs_resolve (fs, subvol, path, &loc, &iatt);

	if (ret == 0 && stat)
		iatt_to_stat (&iatt, stat);
out:
	loc_wipe (&loc);

	return ret;
}


int
glfs_fstat (struct glfs_fd *glfd, struct stat *stat)
{
	int              ret = -1;
	xlator_t        *subvol = NULL;
	struct iatt      iatt = {0, };

	__glfs_entry_fd (glfd);

	subvol = glfs_fd_subvol (glfd);
	if (!subvol) {
		ret = -1;
		errno = EIO;
		goto out;
	}

	ret = syncop_fstat (subvol, glfd->fd, &iatt);

	if (ret == 0 && stat)
		iatt_to_stat (&iatt, stat);
out:
	return ret;
}


struct glfs_fd *
glfs_creat (struct glfs *fs, const char *path, int flags, mode_t mode)
{
	int              ret = -1;
	struct glfs_fd  *glfd = NULL;
	xlator_t        *subvol = NULL;
	loc_t            loc = {0, };
	struct iatt      iatt = {0, };
	uuid_t           gfid;
	dict_t          *xattr_req = NULL;

	__glfs_entry_fs (fs);

	subvol = glfs_active_subvol (fs);
	if (!subvol) {
		ret = -1;
		errno = EIO;
		goto out;
	}

	xattr_req = dict_new ();
	if (!xattr_req) {
		ret = -1;
		errno = ENOMEM;
		goto out;
	}

	uuid_generate (gfid);
	ret = dict_set_static_bin (xattr_req, "gfid-req", gfid, 16);
	if (ret) {
		ret = -1;
		errno = ENOMEM;
		goto out;
	}

	glfd = GF_CALLOC (1, sizeof (*glfd), glfs_mt_glfs_fd_t);
	if (!glfd)
		goto out;

	ret = glfs_resolve (fs, subvol, path, &loc, &iatt);
	if (ret == -1 && errno != ENOENT)
		/* Any other type of error is fatal */
		goto out;

	if (ret == -1 && errno == ENOENT && !loc.parent)
		/* The parent directory or an ancestor even
		   higher does not exist
		*/
		goto out;

	if (loc.inode) {
		if (flags & O_EXCL) {
			ret = -1;
			errno = EEXIST;
			goto out;
		}

		if (IA_ISDIR (iatt.ia_type)) {
			ret = -1;
			errno = EISDIR;
			goto out;
		}

		if (!IA_ISREG (iatt.ia_type)) {
			ret = -1;
			errno = EINVAL;
			goto out;
		}
	}

	if (ret == -1 && errno == ENOENT) {
		loc.inode = inode_new (loc.parent->table);
		if (!loc.inode) {
			ret = -1;
			errno = ENOMEM;
			goto out;
		}
	}

	glfd->fd = fd_create (loc.inode, getpid());
	if (!glfd->fd) {
		ret = -1;
		errno = ENOMEM;
		goto out;
	}

	ret = syncop_create (subvol, &loc, flags, mode, glfd->fd, xattr_req);
out:
	loc_wipe (&loc);

	if (xattr_req)
		dict_destroy (xattr_req);

	if (ret && glfd) {
		glfs_fd_destroy (glfd);
		glfd = NULL;
	}

	return glfd;
}


off_t
glfs_lseek (struct glfs_fd *glfd, off_t offset, int whence)
{
	struct stat sb = {0, };
	int         ret = -1;

	__glfs_entry_fd (glfd);

	switch (whence) {
	case SEEK_SET:
		glfd->offset = offset;
		break;
	case SEEK_CUR:
		glfd->offset += offset;
		break;
	case SEEK_END:
		ret = glfs_fstat (glfd, &sb);
		if (ret) {
			/* seek cannot fail :O */
			break;
		}
		glfd->offset = sb.st_size + offset;
		break;
	}

	return glfd->offset;
}


//////////////

ssize_t
glfs_preadv (struct glfs_fd *glfd, const struct iovec *iovec, int iovcnt,
	     off_t offset, int flags)
{
	xlator_t       *subvol = NULL;
	int             ret = -1;
	size_t          size = -1;
	struct iovec   *iov = NULL;
	int             cnt = 0;
	struct iobref  *iobref = NULL;

	__glfs_entry_fd (glfd);

	subvol = glfs_fd_subvol (glfd);

	size = iov_length (iovec, iovcnt);

	ret = syncop_readv (subvol, glfd->fd, size, offset,
			    0, &iov, &cnt, &iobref);
	if (ret <= 0)
		return ret;

	size = iov_copy (iovec, iovcnt, iov, cnt); /* FIXME!!! */

	glfd->offset = (offset + size);

	if (iov)
		GF_FREE (iov);
	if (iobref)
		iobref_unref (iobref);

	return size;
}


ssize_t
glfs_read (struct glfs_fd *glfd, void *buf, size_t count, int flags)
{
	struct iovec iov = {0, };
	ssize_t      ret = 0;

	iov.iov_base = buf;
	iov.iov_len = count;

	ret = glfs_preadv (glfd, &iov, 1, glfd->offset, flags);

	return ret;
}


ssize_t
glfs_pread (struct glfs_fd *glfd, void *buf, size_t count, off_t offset,
	    int flags)
{
	struct iovec iov = {0, };
	ssize_t      ret = 0;

	iov.iov_base = buf;
	iov.iov_len = count;

	ret = glfs_preadv (glfd, &iov, 1, offset, flags);

	return ret;
}


ssize_t
glfs_readv (struct glfs_fd *glfd, const struct iovec *iov, int count,
	    int flags)
{
	ssize_t      ret = 0;

	ret = glfs_preadv (glfd, iov, count, glfd->offset, flags);

	return ret;
}


struct glfs_io {
	struct glfs_fd      *glfd;
	int                  op;
	off_t                offset;
	struct iovec        *iov;
	int                  count;
	int                  flags;
	glfs_io_cbk          fn;
	void                *data;
};


static int
glfs_io_async_cbk (int ret, call_frame_t *frame, void *data)
{
	struct glfs_io  *gio = data;

	gio->fn (gio->glfd, ret, gio->data);

	GF_FREE (gio->iov);
	GF_FREE (gio);

	return 0;
}


static int
glfs_io_async_task (void *data)
{
	struct glfs_io *gio = data;
	ssize_t         ret = 0;

	switch (gio->op) {
	case GF_FOP_READ:
		ret = glfs_preadv (gio->glfd, gio->iov, gio->count,
				   gio->offset, gio->flags);
		break;
	case GF_FOP_WRITE:
		ret = glfs_pwritev (gio->glfd, gio->iov, gio->count,
				    gio->offset, gio->flags);
		break;
	case GF_FOP_FTRUNCATE:
		ret = glfs_ftruncate (gio->glfd, gio->offset);
		break;
	case GF_FOP_FSYNC:
		if (gio->flags)
			ret = glfs_fdatasync (gio->glfd);
		else
			ret = glfs_fsync (gio->glfd);
		break;
	}

	return (int) ret;
}


int
glfs_preadv_async (struct glfs_fd *glfd, const struct iovec *iovec, int count,
		   off_t offset, int flags, glfs_io_cbk fn, void *data)
{
	struct glfs_io *gio = NULL;
	int             ret = 0;

	gio = GF_CALLOC (1, sizeof (*gio), glfs_mt_glfs_io_t);
	if (!gio) {
		errno = ENOMEM;
		return -1;
	}

	gio->iov = iov_dup (iovec, count);
	if (!gio->iov) {
		GF_FREE (gio);
		errno = ENOMEM;
		return -1;
	}

	gio->op     = GF_FOP_READ;
	gio->glfd   = glfd;
	gio->count  = count;
	gio->offset = offset;
	gio->flags  = flags;
	gio->fn     = fn;
	gio->data   = data;

	ret = synctask_new (glfs_from_glfd (glfd)->ctx->env,
			    glfs_io_async_task, glfs_io_async_cbk,
			    NULL, gio);

	if (ret) {
		GF_FREE (gio->iov);
		GF_FREE (gio);
	}

	return ret;
}


int
glfs_read_async (struct glfs_fd *glfd, void *buf, size_t count, int flags,
		 glfs_io_cbk fn, void *data)
{
	struct iovec iov = {0, };
	ssize_t      ret = 0;

	iov.iov_base = buf;
	iov.iov_len = count;

	ret = glfs_preadv_async (glfd, &iov, 1, glfd->offset, flags, fn, data);

	return ret;
}


int
glfs_pread_async (struct glfs_fd *glfd, void *buf, size_t count, off_t offset,
		  int flags, glfs_io_cbk fn, void *data)
{
	struct iovec iov = {0, };
	ssize_t      ret = 0;

	iov.iov_base = buf;
	iov.iov_len = count;

	ret = glfs_preadv_async (glfd, &iov, 1, offset, flags, fn, data);

	return ret;
}


int
glfs_readv_async (struct glfs_fd *glfd, const struct iovec *iov, int count,
		  int flags, glfs_io_cbk fn, void *data)
{
	ssize_t      ret = 0;

	ret = glfs_preadv_async (glfd, iov, count, glfd->offset, flags,
				 fn, data);
	return ret;
}

///// writev /////

ssize_t
glfs_pwritev (struct glfs_fd *glfd, const struct iovec *iovec, int iovcnt,
	      off_t offset, int flags)
{
	xlator_t       *subvol = NULL;
	int             ret = -1;
	size_t          size = -1;
	struct iobref  *iobref = NULL;
	struct iobuf   *iobuf = NULL;
	struct iovec    iov = {0, };

	__glfs_entry_fd (glfd);

	subvol = glfs_fd_subvol (glfd);

	size = iov_length (iovec, iovcnt);

	iobuf = iobuf_get2 (subvol->ctx->iobuf_pool, size);
	if (!iobuf) {
		errno = ENOMEM;
		return -1;
	}

	iobref = iobref_new ();
	if (!iobref) {
		iobuf_unref (iobuf);
		errno = ENOMEM;
		return -1;
	}

	ret = iobref_add (iobref, iobuf);
	if (ret) {
		iobuf_unref (iobuf);
		iobref_unref (iobref);
		errno = ENOMEM;
		return -1;
	}

	iov_unload (iobuf_ptr (iobuf), iovec, iovcnt);  /* FIXME!!! */

	iov.iov_base = iobuf_ptr (iobuf);
	iov.iov_len = size;

	ret = syncop_writev (subvol, glfd->fd, &iov, 1, offset,
			     iobref, flags);

	iobuf_unref (iobuf);
	iobref_unref (iobref);

	if (ret <= 0)
		return ret;

	glfd->offset = (offset + size);

	return ret;
}


ssize_t
glfs_write (struct glfs_fd *glfd, const void *buf, size_t count, int flags)
{
	struct iovec iov = {0, };
	ssize_t      ret = 0;

	iov.iov_base = (void *) buf;
	iov.iov_len = count;

	ret = glfs_pwritev (glfd, &iov, 1, glfd->offset, flags);

	return ret;
}



ssize_t
glfs_writev (struct glfs_fd *glfd, const struct iovec *iov, int count,
	     int flags)
{
	ssize_t      ret = 0;

	ret = glfs_pwritev (glfd, iov, count, glfd->offset, flags);

	return ret;
}


ssize_t
glfs_pwrite (struct glfs_fd *glfd, const void *buf, size_t count, off_t offset,
	     int flags)
{
	struct iovec iov = {0, };
	ssize_t      ret = 0;

	iov.iov_base = (void *) buf;
	iov.iov_len = count;

	ret = glfs_pwritev (glfd, &iov, 1, offset, flags);

	return ret;
}


int
glfs_pwritev_async (struct glfs_fd *glfd, const struct iovec *iovec, int count,
		    off_t offset, int flags, glfs_io_cbk fn, void *data)
{
	struct glfs_io *gio = NULL;
	int             ret = 0;

	gio = GF_CALLOC (1, sizeof (*gio), glfs_mt_glfs_io_t);
	if (!gio) {
		errno = ENOMEM;
		return -1;
	}

	gio->iov = iov_dup (iovec, count);
	if (!gio->iov) {
		GF_FREE (gio);
		errno = ENOMEM;
		return -1;
	}

	gio->op     = GF_FOP_WRITE;
	gio->glfd   = glfd;
	gio->count  = count;
	gio->offset = offset;
	gio->flags  = flags;
	gio->fn     = fn;
	gio->data   = data;

	ret = synctask_new (glfs_from_glfd (glfd)->ctx->env,
			    glfs_io_async_task, glfs_io_async_cbk,
			    NULL, gio);

	if (ret) {
		GF_FREE (gio->iov);
		GF_FREE (gio);
	}

	return ret;
}


int
glfs_write_async (struct glfs_fd *glfd, const void *buf, size_t count, int flags,
		  glfs_io_cbk fn, void *data)
{
	struct iovec iov = {0, };
	ssize_t      ret = 0;

	iov.iov_base = (void *) buf;
	iov.iov_len = count;

	ret = glfs_pwritev_async (glfd, &iov, 1, glfd->offset, flags, fn, data);

	return ret;
}


int
glfs_pwrite_async (struct glfs_fd *glfd, const void *buf, int count,
		   off_t offset, int flags, glfs_io_cbk fn, void *data)
{
	struct iovec iov = {0, };
	ssize_t      ret = 0;

	iov.iov_base = (void *) buf;
	iov.iov_len = count;

	ret = glfs_pwritev_async (glfd, &iov, 1, offset, flags, fn, data);

	return ret;
}


int
glfs_writev_async (struct glfs_fd *glfd, const struct iovec *iov, int count,
		   int flags, glfs_io_cbk fn, void *data)
{
	ssize_t      ret = 0;

	ret = glfs_pwritev_async (glfd, iov, count, glfd->offset, flags,
				  fn, data);
	return ret;
}


int
glfs_fsync (struct glfs_fd *glfd)
{
	int              ret = -1;
	xlator_t        *subvol = NULL;

	__glfs_entry_fd (glfd);

	subvol = glfs_fd_subvol (glfd);
	if (!subvol) {
		ret = -1;
		errno = EIO;
		goto out;
	}

	ret = syncop_fsync (subvol, glfd->fd, 0);
out:
	return ret;
}


static int
glfs_fsync_async_common (struct glfs_fd *glfd, glfs_io_cbk fn, void *data,
			 int dataonly)
{
	struct glfs_io *gio = NULL;
	int             ret = 0;

	gio = GF_CALLOC (1, sizeof (*gio), glfs_mt_glfs_io_t);
	if (!gio) {
		errno = ENOMEM;
		return -1;
	}

	gio->op     = GF_FOP_FSYNC;
	gio->glfd   = glfd;
	gio->flags  = dataonly;
	gio->fn     = fn;
	gio->data   = data;

	ret = synctask_new (glfs_from_glfd (glfd)->ctx->env,
			    glfs_io_async_task, glfs_io_async_cbk,
			    NULL, gio);

	if (ret) {
		GF_FREE (gio->iov);
		GF_FREE (gio);
	}

	return ret;

}


int
glfs_fsync_async (struct glfs_fd *glfd, glfs_io_cbk fn, void *data)
{
	return glfs_fsync_async_common (glfd, fn, data, 0);
}


int
glfs_fdatasync (struct glfs_fd *glfd)
{
	int              ret = -1;
	xlator_t        *subvol = NULL;

	__glfs_entry_fd (glfd);

	subvol = glfs_fd_subvol (glfd);
	if (!subvol) {
		ret = -1;
		errno = EIO;
		goto out;
	}

	ret = syncop_fsync (subvol, glfd->fd, 1);
out:
	return ret;
}


int
glfs_fdatasync_async (struct glfs_fd *glfd, glfs_io_cbk fn, void *data)
{
	return glfs_fsync_async_common (glfd, fn, data, 1);
}


int
glfs_ftruncate (struct glfs_fd *glfd, off_t offset)
{
	int              ret = -1;
	xlator_t        *subvol = NULL;

	__glfs_entry_fd (glfd);

	subvol = glfs_fd_subvol (glfd);
	if (!subvol) {
		ret = -1;
		errno = EIO;
		goto out;
	}

	ret = syncop_ftruncate (subvol, glfd->fd, offset);
out:
	return ret;
}


int
glfs_ftruncate_async (struct glfs_fd *glfd, off_t offset,
		      glfs_io_cbk fn, void *data)
{
	struct glfs_io *gio = NULL;
	int             ret = 0;

	gio = GF_CALLOC (1, sizeof (*gio), glfs_mt_glfs_io_t);
	if (!gio) {
		errno = ENOMEM;
		return -1;
	}

	gio->op     = GF_FOP_FTRUNCATE;
	gio->glfd   = glfd;
	gio->offset = offset;
	gio->fn     = fn;
	gio->data   = data;

	ret = synctask_new (glfs_from_glfd (glfd)->ctx->env,
			    glfs_io_async_task, glfs_io_async_cbk,
			    NULL, gio);

	if (ret) {
		GF_FREE (gio->iov);
		GF_FREE (gio);
	}

	return ret;
}

