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
#include <limits.h>

#ifdef NAME_MAX
#define GF_NAME_MAX NAME_MAX
#else
#define GF_NAME_MAX 255
#endif

#define READDIRBUF_SIZE (sizeof(struct dirent) + GF_NAME_MAX + 1)

int
glfs_loc_link (loc_t *loc, struct iatt *iatt)
{
	int ret = -1;
	inode_t *linked_inode = NULL;

	if (!loc->inode) {
		errno = EINVAL;
		return -1;
	}

	linked_inode = inode_link (loc->inode, loc->parent, loc->name, iatt);
	if (linked_inode) {
		inode_lookup (linked_inode);
		inode_unref (linked_inode);
		ret = 0;
	} else {
		ret = -1;
		errno = ENOMEM;
	}

	return ret;
}


void
glfs_iatt_to_stat (struct glfs *fs, struct iatt *iatt, struct stat *stat)
{
	iatt_to_stat (iatt, stat);
	stat->st_dev = fs->dev_id;
}


int
glfs_loc_unlink (loc_t *loc)
{
	inode_unlink (loc->inode, loc->parent, loc->name);

	return 0;
}


struct glfs_fd *
glfs_open (struct glfs *fs, const char *path, int flags)
{
	int              ret = -1;
	struct glfs_fd  *glfd = NULL;
	xlator_t        *subvol = NULL;
	loc_t            loc = {0, };
	struct iatt      iatt = {0, };
	int              reval = 0;

	__glfs_entry_fs (fs);

	subvol = glfs_active_subvol (fs);
	if (!subvol) {
		ret = -1;
		errno = EIO;
		goto out;
	}

	glfd = glfs_fd_new (fs);
	if (!glfd)
		goto out;

retry:
	ret = glfs_resolve (fs, subvol, path, &loc, &iatt, reval);

	ESTALE_RETRY (ret, errno, reval, &loc, retry);

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

	if (glfd->fd) {
		/* Retry. Safe to touch glfd->fd as we
		   still have not glfs_fd_bind() yet.
		*/
		fd_unref (glfd->fd);
		glfd->fd = NULL;
	}

	glfd->fd = fd_create (loc.inode, getpid());
	if (!glfd->fd) {
		ret = -1;
		errno = ENOMEM;
		goto out;
	}

	ret = syncop_open (subvol, &loc, flags, glfd->fd);
        DECODE_SYNCOP_ERR (ret);

	ESTALE_RETRY (ret, errno, reval, &loc, retry);
out:
	loc_wipe (&loc);

	if (ret && glfd) {
		glfs_fd_destroy (glfd);
		glfd = NULL;
	} else if (glfd) {
                glfd->fd->flags = flags;
		fd_bind (glfd->fd);
		glfs_fd_bind (glfd);
	}

	glfs_subvol_done (fs, subvol);

	return glfd;
}


int
glfs_close (struct glfs_fd *glfd)
{
	xlator_t  *subvol = NULL;
	int        ret = -1;
	fd_t      *fd = NULL;
	struct glfs *fs = NULL;

	__glfs_entry_fd (glfd);

	subvol = glfs_active_subvol (glfd->fs);
        if (!subvol) {
                ret = -1;
                errno = EIO;
                goto out;
        }

	fd = glfs_resolve_fd (glfd->fs, subvol, glfd);
	if (!fd) {
		ret = -1;
		errno = EBADFD;
		goto out;
	}

	ret = syncop_flush (subvol, fd);
        DECODE_SYNCOP_ERR (ret);
out:
	fs = glfd->fs;
	glfs_fd_destroy (glfd);

	if (fd)
		fd_unref (fd);

	glfs_subvol_done (fs, subvol);

	return ret;
}


int
glfs_lstat (struct glfs *fs, const char *path, struct stat *stat)
{
	int              ret = -1;
	xlator_t        *subvol = NULL;
	loc_t            loc = {0, };
	struct iatt      iatt = {0, };
	int              reval = 0;

	__glfs_entry_fs (fs);

	subvol = glfs_active_subvol (fs);
	if (!subvol) {
		ret = -1;
		errno = EIO;
		goto out;
	}
retry:
	ret = glfs_lresolve (fs, subvol, path, &loc, &iatt, reval);

	ESTALE_RETRY (ret, errno, reval, &loc, retry);

	if (ret == 0 && stat)
		glfs_iatt_to_stat (fs, &iatt, stat);
out:
	loc_wipe (&loc);

	glfs_subvol_done (fs, subvol);

	return ret;
}


int
glfs_stat (struct glfs *fs, const char *path, struct stat *stat)
{
	int              ret = -1;
	xlator_t        *subvol = NULL;
	loc_t            loc = {0, };
	struct iatt      iatt = {0, };
	int              reval = 0;

	__glfs_entry_fs (fs);

	subvol = glfs_active_subvol (fs);
	if (!subvol) {
		ret = -1;
		errno = EIO;
		goto out;
	}
retry:
	ret = glfs_resolve (fs, subvol, path, &loc, &iatt, reval);

	ESTALE_RETRY (ret, errno, reval, &loc, retry);

	if (ret == 0 && stat)
		glfs_iatt_to_stat (fs, &iatt, stat);
out:
	loc_wipe (&loc);

	glfs_subvol_done (fs, subvol);

	return ret;
}


int
glfs_fstat (struct glfs_fd *glfd, struct stat *stat)
{
	int              ret = -1;
	xlator_t        *subvol = NULL;
	struct iatt      iatt = {0, };
	fd_t            *fd = NULL;

	__glfs_entry_fd (glfd);

	subvol = glfs_active_subvol (glfd->fs);
	if (!subvol) {
		ret = -1;
		errno = EIO;
		goto out;
	}

	fd = glfs_resolve_fd (glfd->fs, subvol, glfd);
	if (!fd) {
		ret = -1;
		errno = EBADFD;
		goto out;
	}

	ret = syncop_fstat (subvol, fd, &iatt);
        DECODE_SYNCOP_ERR (ret);

	if (ret == 0 && stat)
		glfs_iatt_to_stat (glfd->fs, &iatt, stat);
out:
	if (fd)
		fd_unref (fd);

	glfs_subvol_done (glfd->fs, subvol);

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
	int              reval = 0;

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

	glfd = glfs_fd_new (fs);
	if (!glfd)
		goto out;

	/* This must be glfs_resolve() and NOT glfs_lresolve().
	   That is because open("name", O_CREAT) where "name"
	   is a danging symlink must create the dangling
	   destinataion.
	*/
retry:
	ret = glfs_resolve (fs, subvol, path, &loc, &iatt, reval);

	ESTALE_RETRY (ret, errno, reval, &loc, retry);

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

	if (glfd->fd) {
		/* Retry. Safe to touch glfd->fd as we
		   still have not glfs_fd_bind() yet.
		*/
		fd_unref (glfd->fd);
		glfd->fd = NULL;
	}

	glfd->fd = fd_create (loc.inode, getpid());
	if (!glfd->fd) {
		ret = -1;
		errno = ENOMEM;
		goto out;
	}

	if (ret == 0) {
		ret = syncop_open (subvol, &loc, flags, glfd->fd);
                DECODE_SYNCOP_ERR (ret);
	} else {
		ret = syncop_create (subvol, &loc, flags, mode, glfd->fd,
				     xattr_req, &iatt);
                DECODE_SYNCOP_ERR (ret);
	}

	ESTALE_RETRY (ret, errno, reval, &loc, retry);

	if (ret == 0)
		ret = glfs_loc_link (&loc, &iatt);
out:
	loc_wipe (&loc);

	if (xattr_req)
		dict_unref (xattr_req);

	if (ret && glfd) {
		glfs_fd_destroy (glfd);
		glfd = NULL;
	} else if (glfd) {
                glfd->fd->flags = flags;
		fd_bind (glfd->fd);
		glfs_fd_bind (glfd);
	}

	glfs_subvol_done (fs, subvol);

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
	ssize_t         ret = -1;
	ssize_t         size = -1;
	struct iovec   *iov = NULL;
	int             cnt = 0;
	struct iobref  *iobref = NULL;
	fd_t           *fd = NULL;

	__glfs_entry_fd (glfd);

	subvol = glfs_active_subvol (glfd->fs);
	if (!subvol) {
		ret = -1;
		errno = EIO;
		goto out;
	}

	fd = glfs_resolve_fd (glfd->fs, subvol, glfd);
	if (!fd) {
		ret = -1;
		errno = EBADFD;
		goto out;
	}

	size = iov_length (iovec, iovcnt);

	ret = syncop_readv (subvol, fd, size, offset, 0, &iov, &cnt, &iobref);
        DECODE_SYNCOP_ERR (ret);
	if (ret <= 0)
		goto out;

	size = iov_copy (iovec, iovcnt, iov, cnt); /* FIXME!!! */

	glfd->offset = (offset + size);

	ret = size;
out:
        if (iov)
                GF_FREE (iov);
        if (iobref)
                iobref_unref (iobref);

	if (fd)
		fd_unref (fd);

	glfs_subvol_done (glfd->fs, subvol);

	return ret;
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
	case GF_FOP_DISCARD:
		ret = glfs_discard (gio->glfd, gio->offset, gio->count);
		break;
        case GF_FOP_ZEROFILL:
                ret = glfs_zerofill(gio->glfd, gio->offset, gio->count);
                break;
	}

	return (int) ret;
}


int
glfs_preadv_async_cbk (call_frame_t *frame, void *cookie, xlator_t *this,
		       int op_ret, int op_errno, struct iovec *iovec,
		       int count, struct iatt *stbuf, struct iobref *iobref,
		       dict_t *xdata)
{
	struct glfs_io *gio = NULL;
	xlator_t       *subvol = NULL;
	struct glfs    *fs = NULL;
	struct glfs_fd *glfd = NULL;


	gio = frame->local;
	frame->local = NULL;
	subvol = cookie;
	glfd = gio->glfd;
	fs = glfd->fs;

	if (op_ret <= 0)
		goto out;

	op_ret = iov_copy (gio->iov, gio->count, iovec, count);

	glfd->offset = gio->offset + op_ret;
out:
	errno = op_errno;
	gio->fn (gio->glfd, op_ret, gio->data);

	GF_FREE (gio->iov);
	GF_FREE (gio);
	STACK_DESTROY (frame->root);
	glfs_subvol_done (fs, subvol);

	return 0;
}


int
glfs_preadv_async (struct glfs_fd *glfd, const struct iovec *iovec, int count,
		   off_t offset, int flags, glfs_io_cbk fn, void *data)
{
	struct glfs_io *gio = NULL;
	int             ret = 0;
	call_frame_t   *frame = NULL;
	xlator_t       *subvol = NULL;
	glfs_t         *fs = NULL;
	fd_t           *fd = NULL;

	__glfs_entry_fd (glfd);

	subvol = glfs_active_subvol (glfd->fs);
	if (!subvol) {
		ret = -1;
		errno = EIO;
		goto out;
	}

	fd = glfs_resolve_fd (glfd->fs, subvol, glfd);
	if (!fd) {
		ret = -1;
		errno = EBADFD;
		goto out;
	}

	fs = glfd->fs;

	frame = syncop_create_frame (THIS);
	if (!frame) {
		ret = -1;
		errno = ENOMEM;
		goto out;
	}

	gio = GF_CALLOC (1, sizeof (*gio), glfs_mt_glfs_io_t);
	if (!gio) {
		ret = -1;
		errno = ENOMEM;
		goto out;
	}

	gio->iov = iov_dup (iovec, count);
	if (!gio->iov) {
		ret = -1;
		errno = ENOMEM;
		goto out;
	}

	gio->op     = GF_FOP_READ;
	gio->glfd   = glfd;
	gio->count  = count;
	gio->offset = offset;
	gio->flags  = flags;
	gio->fn     = fn;
	gio->data   = data;

	frame->local = gio;

	STACK_WIND_COOKIE (frame, glfs_preadv_async_cbk, subvol, subvol,
			   subvol->fops->readv, fd, iov_length (iovec, count),
			   offset, flags, NULL);

out:
        if (ret) {
                if (gio) {
                        GF_FREE (gio->iov);
                        GF_FREE (gio);
                }
                if (frame) {
                        STACK_DESTROY (frame->root);
                }
		glfs_subvol_done (fs, subvol);
	}

	if (fd)
		fd_unref (fd);

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
	fd_t           *fd = NULL;

	__glfs_entry_fd (glfd);

	subvol = glfs_active_subvol (glfd->fs);
	if (!subvol) {
		ret = -1;
		errno = EIO;
		goto out;
	}

	fd = glfs_resolve_fd (glfd->fs, subvol, glfd);
	if (!fd) {
		ret = -1;
		errno = EBADFD;
		goto out;
	}

	size = iov_length (iovec, iovcnt);

	iobuf = iobuf_get2 (subvol->ctx->iobuf_pool, size);
	if (!iobuf) {
		ret = -1;
		errno = ENOMEM;
		goto out;
	}

	iobref = iobref_new ();
	if (!iobref) {
		iobuf_unref (iobuf);
		errno = ENOMEM;
		ret = -1;
		goto out;
	}

	ret = iobref_add (iobref, iobuf);
	if (ret) {
		iobuf_unref (iobuf);
		iobref_unref (iobref);
		errno = ENOMEM;
		ret = -1;
		goto out;
	}

	iov_unload (iobuf_ptr (iobuf), iovec, iovcnt);  /* FIXME!!! */

	iov.iov_base = iobuf_ptr (iobuf);
	iov.iov_len = size;

	ret = syncop_writev (subvol, fd, &iov, 1, offset, iobref, flags);
        DECODE_SYNCOP_ERR (ret);

	iobuf_unref (iobuf);
	iobref_unref (iobref);

	if (ret <= 0)
		goto out;

	glfd->offset = (offset + size);

out:
	if (fd)
		fd_unref (fd);

	glfs_subvol_done (glfd->fs, subvol);

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
	fd_t            *fd = NULL;

	__glfs_entry_fd (glfd);

	subvol = glfs_active_subvol (glfd->fs);
	if (!subvol) {
		ret = -1;
		errno = EIO;
		goto out;
	}

	fd = glfs_resolve_fd (glfd->fs, subvol, glfd);
	if (!fd) {
		ret = -1;
		errno = EBADFD;
		goto out;
	}

	ret = syncop_fsync (subvol, fd, 0);
        DECODE_SYNCOP_ERR (ret);
out:
	if (fd)
		fd_unref (fd);

	glfs_subvol_done (glfd->fs, subvol);

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
	fd_t            *fd = NULL;

	__glfs_entry_fd (glfd);

	subvol = glfs_active_subvol (glfd->fs);
	if (!subvol) {
		ret = -1;
		errno = EIO;
		goto out;
	}

	fd = glfs_resolve_fd (glfd->fs, subvol, glfd);
	if (!fd) {
		ret = -1;
		errno = EBADFD;
		goto out;
	}

	ret = syncop_fsync (subvol, fd, 1);
        DECODE_SYNCOP_ERR (ret);
out:
	if (fd)
		fd_unref (fd);

	glfs_subvol_done (glfd->fs, subvol);

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
	fd_t            *fd = NULL;

	__glfs_entry_fd (glfd);

	subvol = glfs_active_subvol (glfd->fs);
	if (!subvol) {
		ret = -1;
		errno = EIO;
		goto out;
	}

	fd = glfs_resolve_fd (glfd->fs, subvol, glfd);
	if (!fd) {
		ret = -1;
		errno = EBADFD;
		goto out;
	}

	ret = syncop_ftruncate (subvol, fd, offset);
        DECODE_SYNCOP_ERR (ret);
out:
	if (fd)
		fd_unref (fd);

	glfs_subvol_done (glfd->fs, subvol);

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


int
glfs_access (struct glfs *fs, const char *path, int mode)
{
	int              ret = -1;
	xlator_t        *subvol = NULL;
	loc_t            loc = {0, };
	struct iatt      iatt = {0, };
	int              reval = 0;

	__glfs_entry_fs (fs);

	subvol = glfs_active_subvol (fs);
	if (!subvol) {
		ret = -1;
		errno = EIO;
		goto out;
	}
retry:
	ret = glfs_resolve (fs, subvol, path, &loc, &iatt, reval);

	ESTALE_RETRY (ret, errno, reval, &loc, retry);

	if (ret)
		goto out;

	ret = syncop_access (subvol, &loc, mode);
        DECODE_SYNCOP_ERR (ret);

	ESTALE_RETRY (ret, errno, reval, &loc, retry);
out:
	loc_wipe (&loc);

	glfs_subvol_done (fs, subvol);

	return ret;
}


int
glfs_symlink (struct glfs *fs, const char *data, const char *path)
{
	int              ret = -1;
	xlator_t        *subvol = NULL;
	loc_t            loc = {0, };
	struct iatt      iatt = {0, };
	uuid_t           gfid;
	dict_t          *xattr_req = NULL;
	int              reval = 0;

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
retry:
	ret = glfs_lresolve (fs, subvol, path, &loc, &iatt, reval);

	ESTALE_RETRY (ret, errno, reval, &loc, retry);

	if (loc.inode) {
		errno = EEXIST;
		ret = -1;
		goto out;
	}

	if (ret == -1 && errno != ENOENT)
		/* Any other type of error is fatal */
		goto out;

	if (ret == -1 && errno == ENOENT && !loc.parent)
		/* The parent directory or an ancestor even
		   higher does not exist
		*/
		goto out;

	/* ret == -1 && errno == ENOENT */
	loc.inode = inode_new (loc.parent->table);
	if (!loc.inode) {
		ret = -1;
		errno = ENOMEM;
		goto out;
	}

	ret = syncop_symlink (subvol, &loc, data, xattr_req, &iatt);
        DECODE_SYNCOP_ERR (ret);

	ESTALE_RETRY (ret, errno, reval, &loc, retry);

	if (ret == 0)
		ret = glfs_loc_link (&loc, &iatt);
out:
	loc_wipe (&loc);

	if (xattr_req)
		dict_unref (xattr_req);

	glfs_subvol_done (fs, subvol);

	return ret;
}


int
glfs_readlink (struct glfs *fs, const char *path, char *buf, size_t bufsiz)
{
	int              ret = -1;
	xlator_t        *subvol = NULL;
	loc_t            loc = {0, };
	struct iatt      iatt = {0, };
	int              reval = 0;
	char            *linkval = NULL;

	__glfs_entry_fs (fs);

	subvol = glfs_active_subvol (fs);
	if (!subvol) {
		ret = -1;
		errno = EIO;
		goto out;
	}
retry:
	ret = glfs_lresolve (fs, subvol, path, &loc, &iatt, reval);

	ESTALE_RETRY (ret, errno, reval, &loc, retry);

	if (ret)
		goto out;

	if (iatt.ia_type != IA_IFLNK) {
		ret = -1;
		errno = EINVAL;
		goto out;
	}

	ret = syncop_readlink (subvol, &loc, &linkval, bufsiz);
        DECODE_SYNCOP_ERR (ret);
	if (ret > 0) {
		memcpy (buf, linkval, ret);
		GF_FREE (linkval);
	}

	ESTALE_RETRY (ret, errno, reval, &loc, retry);
out:
	loc_wipe (&loc);

	glfs_subvol_done (fs, subvol);

	return ret;
}


int
glfs_mknod (struct glfs *fs, const char *path, mode_t mode, dev_t dev)
{
	int              ret = -1;
	xlator_t        *subvol = NULL;
	loc_t            loc = {0, };
	struct iatt      iatt = {0, };
	uuid_t           gfid;
	dict_t          *xattr_req = NULL;
	int              reval = 0;

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
retry:
	ret = glfs_lresolve (fs, subvol, path, &loc, &iatt, reval);

	ESTALE_RETRY (ret, errno, reval, &loc, retry);

	if (loc.inode) {
		errno = EEXIST;
		ret = -1;
		goto out;
	}

	if (ret == -1 && errno != ENOENT)
		/* Any other type of error is fatal */
		goto out;

	if (ret == -1 && errno == ENOENT && !loc.parent)
		/* The parent directory or an ancestor even
		   higher does not exist
		*/
		goto out;

	/* ret == -1 && errno == ENOENT */
	loc.inode = inode_new (loc.parent->table);
	if (!loc.inode) {
		ret = -1;
		errno = ENOMEM;
		goto out;
	}

	ret = syncop_mknod (subvol, &loc, mode, dev, xattr_req, &iatt);
        DECODE_SYNCOP_ERR (ret);

	ESTALE_RETRY (ret, errno, reval, &loc, retry);

	if (ret == 0)
		ret = glfs_loc_link (&loc, &iatt);
out:
	loc_wipe (&loc);

	if (xattr_req)
		dict_unref (xattr_req);

	glfs_subvol_done (fs, subvol);

	return ret;
}


int
glfs_mkdir (struct glfs *fs, const char *path, mode_t mode)
{
	int              ret = -1;
	xlator_t        *subvol = NULL;
	loc_t            loc = {0, };
	struct iatt      iatt = {0, };
	uuid_t           gfid;
	dict_t          *xattr_req = NULL;
	int              reval = 0;

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
retry:
	ret = glfs_lresolve (fs, subvol, path, &loc, &iatt, reval);

	ESTALE_RETRY (ret, errno, reval, &loc, retry);

	if (loc.inode) {
		errno = EEXIST;
		ret = -1;
		goto out;
	}

	if (ret == -1 && errno != ENOENT)
		/* Any other type of error is fatal */
		goto out;

	if (ret == -1 && errno == ENOENT && !loc.parent)
		/* The parent directory or an ancestor even
		   higher does not exist
		*/
		goto out;

	/* ret == -1 && errno == ENOENT */
	loc.inode = inode_new (loc.parent->table);
	if (!loc.inode) {
		ret = -1;
		errno = ENOMEM;
		goto out;
	}

	ret = syncop_mkdir (subvol, &loc, mode, xattr_req, &iatt);
        DECODE_SYNCOP_ERR (ret);

	ESTALE_RETRY (ret, errno, reval, &loc, retry);

	if (ret == 0)
		ret = glfs_loc_link (&loc, &iatt);
out:
	loc_wipe (&loc);

	if (xattr_req)
		dict_unref (xattr_req);

	glfs_subvol_done (fs, subvol);

	return ret;
}


int
glfs_unlink (struct glfs *fs, const char *path)
{
	int              ret = -1;
	xlator_t        *subvol = NULL;
	loc_t            loc = {0, };
	struct iatt      iatt = {0, };
	int              reval = 0;

	__glfs_entry_fs (fs);

	subvol = glfs_active_subvol (fs);
	if (!subvol) {
		ret = -1;
		errno = EIO;
		goto out;
	}
retry:
	ret = glfs_lresolve (fs, subvol, path, &loc, &iatt, reval);

	ESTALE_RETRY (ret, errno, reval, &loc, retry);

	if (ret)
		goto out;

	if (iatt.ia_type == IA_IFDIR) {
		ret = -1;
		errno = EISDIR;
		goto out;
	}

	ret = syncop_unlink (subvol, &loc);
        DECODE_SYNCOP_ERR (ret);

	ESTALE_RETRY (ret, errno, reval, &loc, retry);

	if (ret == 0)
		ret = glfs_loc_unlink (&loc);
out:
	loc_wipe (&loc);

	glfs_subvol_done (fs, subvol);

	return ret;
}


int
glfs_rmdir (struct glfs *fs, const char *path)
{
	int              ret = -1;
	xlator_t        *subvol = NULL;
	loc_t            loc = {0, };
	struct iatt      iatt = {0, };
	int              reval = 0;

	__glfs_entry_fs (fs);

	subvol = glfs_active_subvol (fs);
	if (!subvol) {
		ret = -1;
		errno = EIO;
		goto out;
	}
retry:
	ret = glfs_lresolve (fs, subvol, path, &loc, &iatt, reval);

	ESTALE_RETRY (ret, errno, reval, &loc, retry);

	if (ret)
		goto out;

	if (iatt.ia_type != IA_IFDIR) {
		ret = -1;
		errno = ENOTDIR;
		goto out;
	}

	ret = syncop_rmdir (subvol, &loc, 0);
        DECODE_SYNCOP_ERR (ret);

	ESTALE_RETRY (ret, errno, reval, &loc, retry);

	if (ret == 0)
		ret = glfs_loc_unlink (&loc);
out:
	loc_wipe (&loc);

	glfs_subvol_done (fs, subvol);

	return ret;
}


int
glfs_rename (struct glfs *fs, const char *oldpath, const char *newpath)
{
	int              ret = -1;
	xlator_t        *subvol = NULL;
	loc_t            oldloc = {0, };
	loc_t            newloc = {0, };
	struct iatt      oldiatt = {0, };
	struct iatt      newiatt = {0, };
	int              reval = 0;

	__glfs_entry_fs (fs);

	subvol = glfs_active_subvol (fs);
	if (!subvol) {
		ret = -1;
		errno = EIO;
		goto out;
	}
retry:
	ret = glfs_lresolve (fs, subvol, oldpath, &oldloc, &oldiatt, reval);

	ESTALE_RETRY (ret, errno, reval, &oldloc, retry);

	if (ret)
		goto out;
retrynew:
	ret = glfs_lresolve (fs, subvol, newpath, &newloc, &newiatt, reval);

	ESTALE_RETRY (ret, errno, reval, &newloc, retrynew);

	if (ret && errno != ENOENT && newloc.parent)
		goto out;

	if (newiatt.ia_type != IA_INVAL) {
                if ((oldiatt.ia_type == IA_IFDIR) !=
                    (newiatt.ia_type == IA_IFDIR)) {
                       /* Either both old and new must be dirs,
                        * or both must be non-dirs. Else, fail.
                        */
                       ret = -1;
                       errno = EISDIR;
                       goto out;
                }
        }

	/* TODO: check if new or old is a prefix of the other, and fail EINVAL */

	ret = syncop_rename (subvol, &oldloc, &newloc);
        DECODE_SYNCOP_ERR (ret);

	if (ret == -1 && errno == ESTALE) {
		if (reval < DEFAULT_REVAL_COUNT) {
			reval++;
			loc_wipe (&oldloc);
			loc_wipe (&newloc);
			goto retry;
		}
	}

	if (ret == 0)
		inode_rename (oldloc.parent->table, oldloc.parent, oldloc.name,
			      newloc.parent, newloc.name, oldloc.inode,
			      &oldiatt);
out:
	loc_wipe (&oldloc);
	loc_wipe (&newloc);

	glfs_subvol_done (fs, subvol);

	return ret;
}


int
glfs_link (struct glfs *fs, const char *oldpath, const char *newpath)
{
	int              ret = -1;
	xlator_t        *subvol = NULL;
	loc_t            oldloc = {0, };
	loc_t            newloc = {0, };
	struct iatt      oldiatt = {0, };
	struct iatt      newiatt = {0, };
	int              reval = 0;

	__glfs_entry_fs (fs);

	subvol = glfs_active_subvol (fs);
	if (!subvol) {
		ret = -1;
		errno = EIO;
		goto out;
	}
retry:
	ret = glfs_lresolve (fs, subvol, oldpath, &oldloc, &oldiatt, reval);

	ESTALE_RETRY (ret, errno, reval, &oldloc, retry);

	if (ret)
		goto out;
retrynew:
	ret = glfs_lresolve (fs, subvol, newpath, &newloc, &newiatt, reval);

	ESTALE_RETRY (ret, errno, reval, &newloc, retrynew);

	if (ret == 0) {
		ret = -1;
		errno = EEXIST;
		goto out;
	}

	if (oldiatt.ia_type == IA_IFDIR) {
		ret = -1;
		errno = EISDIR;
		goto out;
	}

        /* Filling the inode of the hard link to be same as that of the
           original file
        */
	if (newloc.inode) {
		inode_unref (newloc.inode);
		newloc.inode = NULL;
	}
        newloc.inode = inode_ref (oldloc.inode);

	ret = syncop_link (subvol, &oldloc, &newloc);
        DECODE_SYNCOP_ERR (ret);

	if (ret == -1 && errno == ESTALE) {
		loc_wipe (&oldloc);
		loc_wipe (&newloc);
		if (reval--)
			goto retry;
	}

	if (ret == 0)
		ret = glfs_loc_link (&newloc, &oldiatt);
out:
	loc_wipe (&oldloc);
	loc_wipe (&newloc);

	glfs_subvol_done (fs, subvol);

	return ret;
}


struct glfs_fd *
glfs_opendir (struct glfs *fs, const char *path)
{
	int              ret = -1;
	struct glfs_fd  *glfd = NULL;
	xlator_t        *subvol = NULL;
	loc_t            loc = {0, };
	struct iatt      iatt = {0, };
	int              reval = 0;

	__glfs_entry_fs (fs);

	subvol = glfs_active_subvol (fs);
	if (!subvol) {
		ret = -1;
		errno = EIO;
		goto out;
	}

	glfd = glfs_fd_new (fs);
	if (!glfd)
		goto out;

	INIT_LIST_HEAD (&glfd->entries);
retry:
	ret = glfs_resolve (fs, subvol, path, &loc, &iatt, reval);

	ESTALE_RETRY (ret, errno, reval, &loc, retry);

	if (ret)
		goto out;

	if (!IA_ISDIR (iatt.ia_type)) {
		ret = -1;
		errno = ENOTDIR;
		goto out;
	}

	if (glfd->fd) {
		/* Retry. Safe to touch glfd->fd as we
		   still have not glfs_fd_bind() yet.
		*/
		fd_unref (glfd->fd);
		glfd->fd = NULL;
	}

	glfd->fd = fd_create (loc.inode, getpid());
	if (!glfd->fd) {
		ret = -1;
		errno = ENOMEM;
		goto out;
	}

	ret = syncop_opendir (subvol, &loc, glfd->fd);
        DECODE_SYNCOP_ERR (ret);

	ESTALE_RETRY (ret, errno, reval, &loc, retry);
out:
	loc_wipe (&loc);

	if (ret && glfd) {
		glfs_fd_destroy (glfd);
		glfd = NULL;
	} else if (glfd) {
		fd_bind (glfd->fd);
		glfs_fd_bind (glfd);
	}

	glfs_subvol_done (fs, subvol);

	return glfd;
}


int
glfs_closedir (struct glfs_fd *glfd)
{
	__glfs_entry_fd (glfd);

	gf_dirent_free (list_entry (&glfd->entries, gf_dirent_t, list));

	glfs_fd_destroy (glfd);

	return 0;
}


long
glfs_telldir (struct glfs_fd *fd)
{
	return fd->offset;
}


void
glfs_seekdir (struct glfs_fd *fd, long offset)
{
	gf_dirent_t *entry = NULL;
	gf_dirent_t *tmp = NULL;

	if (fd->offset == offset)
		return;

	fd->offset = offset;
	fd->next = NULL;

	list_for_each_entry_safe (entry, tmp, &fd->entries, list) {
		if (entry->d_off != offset)
			continue;

		if (&tmp->list != &fd->entries) {
			/* found! */
			fd->next = tmp;
			return;
		}
	}
	/* could not find entry at requested offset in the cache.
	   next readdir_r() will result in glfd_entry_refresh()
	*/
}

int
glfs_discard_async (struct glfs_fd *glfd, off_t offset, size_t len,
		      glfs_io_cbk fn, void *data)
{
	struct glfs_io *gio = NULL;
	int             ret = 0;

	gio = GF_CALLOC (1, sizeof (*gio), glfs_mt_glfs_io_t);
	if (!gio) {
		errno = ENOMEM;
		return -1;
	}

	gio->op     = GF_FOP_DISCARD;
	gio->glfd   = glfd;
	gio->offset = offset;
	gio->count  = len;
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
glfs_zerofill_async (struct glfs_fd *glfd, off_t offset, off_t len,
                      glfs_io_cbk fn, void *data)
{
        struct glfs_io *gio  = NULL;
        int             ret  = 0;

        gio = GF_CALLOC (1, sizeof (*gio), glfs_mt_glfs_io_t);
        if (!gio) {
                errno = ENOMEM;
                return -1;
        }

        gio->op     = GF_FOP_ZEROFILL;
        gio->glfd   = glfd;
        gio->offset = offset;
        gio->count  = len;
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


void
gf_dirent_to_dirent (gf_dirent_t *gf_dirent, struct dirent *dirent)
{
	dirent->d_ino = gf_dirent->d_ino;

#ifdef _DIRENT_HAVE_D_OFF
	dirent->d_off = gf_dirent->d_off;
#endif

#ifdef _DIRENT_HAVE_D_TYPE
	dirent->d_type = gf_dirent->d_type;
#endif

#ifdef _DIRENT_HAVE_D_NAMLEN
	dirent->d_namlen = strlen (gf_dirent->d_name);
#endif

	strncpy (dirent->d_name, gf_dirent->d_name, GF_NAME_MAX + 1);
}


int
glfd_entry_refresh (struct glfs_fd *glfd, int plus)
{
	xlator_t        *subvol = NULL;
	gf_dirent_t      entries;
	gf_dirent_t      old;
	int              ret = -1;
	fd_t            *fd = NULL;

	subvol = glfs_active_subvol (glfd->fs);
	if (!subvol) {
		ret = -1;
		errno = EIO;
		goto out;
	}

	fd = glfs_resolve_fd (glfd->fs, subvol, glfd);
	if (!fd) {
		ret = -1;
		errno = EBADFD;
		goto out;
	}

	if (fd->inode->ia_type != IA_IFDIR) {
		ret = -1;
		errno = EBADF;
		goto out;
	}

	INIT_LIST_HEAD (&entries.list);
	INIT_LIST_HEAD (&old.list);

	if (plus)
		ret = syncop_readdirp (subvol, fd, 131072, glfd->offset,
				       NULL, &entries);
	else
		ret = syncop_readdir (subvol, fd, 131072, glfd->offset,
				      &entries);
        DECODE_SYNCOP_ERR (ret);
	if (ret >= 0) {
		if (plus)
			gf_link_inodes_from_dirent (THIS, fd->inode, &entries);

		list_splice_init (&glfd->entries, &old.list);
		list_splice_init (&entries.list, &glfd->entries);

		/* spurious errno is dangerous for glfd_entry_next() */
		errno = 0;
	}

	if (ret > 0)
		glfd->next = list_entry (glfd->entries.next, gf_dirent_t, list);

	gf_dirent_free (&old);
out:
	if (fd)
		fd_unref (fd);

	glfs_subvol_done (glfd->fs, subvol);

	return ret;
}


gf_dirent_t *
glfd_entry_next (struct glfs_fd *glfd, int plus)
{
	gf_dirent_t     *entry = NULL;
	int              ret = -1;

	if (!glfd->offset || !glfd->next) {
		ret = glfd_entry_refresh (glfd, plus);
		if (ret < 0)
			return NULL;
	}

	entry = glfd->next;
	if (!entry)
		return NULL;

	if (&entry->next->list == &glfd->entries)
		glfd->next = NULL;
	else
		glfd->next = entry->next;

	glfd->offset = entry->d_off;

	return entry;
}


static struct dirent *
glfs_readdirbuf_get (struct glfs_fd *glfd)
{
        struct dirent *buf = NULL;

        LOCK (&glfd->fd->lock);
        {
                buf = glfd->readdirbuf;
                if (buf) {
                        memset (buf, 0, READDIRBUF_SIZE);
                        goto unlock;
                }

                buf = GF_CALLOC (1, READDIRBUF_SIZE, glfs_mt_readdirbuf_t);
                if (!buf) {
                        errno = ENOMEM;
                        goto unlock;
                }

                glfd->readdirbuf = buf;
        }
unlock:
        UNLOCK (&glfd->fd->lock);

        return buf;
}


int
glfs_readdirplus_r (struct glfs_fd *glfd, struct stat *stat, struct dirent *ext,
		    struct dirent **res)
{
	int              ret = 0;
	gf_dirent_t     *entry = NULL;
	struct dirent   *buf = NULL;

	__glfs_entry_fd (glfd);

	errno = 0;

	if (ext)
		buf = ext;
	else
		buf = glfs_readdirbuf_get (glfd);

	if (!buf) {
		errno = ENOMEM;
		return -1;
	}

	entry = glfd_entry_next (glfd, !!stat);
	if (errno)
		ret = -1;

	if (res) {
		if (entry)
			*res = buf;
		else
			*res = NULL;
	}

	if (entry) {
		gf_dirent_to_dirent (entry, buf);
		if (stat)
			glfs_iatt_to_stat (glfd->fs, &entry->d_stat, stat);
	}

	return ret;
}


int
glfs_readdir_r (struct glfs_fd *glfd, struct dirent *buf, struct dirent **res)
{
	return glfs_readdirplus_r (glfd, 0, buf, res);
}


struct dirent *
glfs_readdirplus (struct glfs_fd *glfd, struct stat *stat)
{
        struct dirent *res = NULL;
        int ret = -1;

        ret = glfs_readdirplus_r (glfd, stat, NULL, &res);
        if (ret)
                return NULL;

        return res;
}



struct dirent *
glfs_readdir (struct glfs_fd *glfd)
{
        return glfs_readdirplus (glfd, NULL);
}


int
glfs_statvfs (struct glfs *fs, const char *path, struct statvfs *buf)
{
	int              ret = -1;
	xlator_t        *subvol = NULL;
	loc_t            loc = {0, };
	struct iatt      iatt = {0, };
	int              reval = 0;

	__glfs_entry_fs (fs);

	subvol = glfs_active_subvol (fs);
	if (!subvol) {
		ret = -1;
		errno = EIO;
		goto out;
	}
retry:
	ret = glfs_resolve (fs, subvol, path, &loc, &iatt, reval);

	ESTALE_RETRY (ret, errno, reval, &loc, retry);

	if (ret)
		goto out;

	ret = syncop_statfs (subvol, &loc, buf);
        DECODE_SYNCOP_ERR (ret);

	ESTALE_RETRY (ret, errno, reval, &loc, retry);
out:
	loc_wipe (&loc);

	glfs_subvol_done (fs, subvol);

	return ret;
}


int
glfs_setattr (struct glfs *fs, const char *path, struct iatt *iatt,
	      int valid, int follow)
{
	int              ret = -1;
	xlator_t        *subvol = NULL;
	loc_t            loc = {0, };
	struct iatt      riatt = {0, };
	int              reval = 0;

	__glfs_entry_fs (fs);

	subvol = glfs_active_subvol (fs);
	if (!subvol) {
		ret = -1;
		errno = EIO;
		goto out;
	}
retry:
	if (follow)
		ret = glfs_resolve (fs, subvol, path, &loc, &riatt, reval);
	else
		ret = glfs_lresolve (fs, subvol, path, &loc, &riatt, reval);

	ESTALE_RETRY (ret, errno, reval, &loc, retry);

	if (ret)
		goto out;

	ret = syncop_setattr (subvol, &loc, iatt, valid, 0, 0);
        DECODE_SYNCOP_ERR (ret);

	ESTALE_RETRY (ret, errno, reval, &loc, retry);
out:
	loc_wipe (&loc);

	glfs_subvol_done (fs, subvol);

	return ret;
}


int
glfs_fsetattr (struct glfs_fd *glfd, struct iatt *iatt, int valid)
{
	int              ret = -1;
	xlator_t        *subvol = NULL;
	fd_t            *fd = NULL;

	__glfs_entry_fd (glfd);

	subvol = glfs_active_subvol (glfd->fs);
	if (!subvol) {
		ret = -1;
		errno = EIO;
		goto out;
	}

	fd = glfs_resolve_fd (glfd->fs, subvol, glfd);
	if (!fd) {
		ret = -1;
		errno = EBADFD;
		goto out;
	}

	ret = syncop_fsetattr (subvol, fd, iatt, valid, 0, 0);
        DECODE_SYNCOP_ERR (ret);
out:
	if (fd)
		fd_unref (fd);

	glfs_subvol_done (glfd->fs, subvol);

	return ret;
}


int
glfs_chmod (struct glfs *fs, const char *path, mode_t mode)
{
	int              ret = -1;
	struct iatt      iatt = {0, };
	int              valid = 0;

	iatt.ia_prot = ia_prot_from_st_mode (mode);
	valid = GF_SET_ATTR_MODE;

	ret = glfs_setattr (fs, path, &iatt, valid, 1);

	return ret;
}


int
glfs_fchmod (struct glfs_fd *glfd, mode_t mode)
{
	int              ret = -1;
	struct iatt      iatt = {0, };
	int              valid = 0;

	iatt.ia_prot = ia_prot_from_st_mode (mode);
	valid = GF_SET_ATTR_MODE;

	ret = glfs_fsetattr (glfd, &iatt, valid);

	return ret;
}


int
glfs_chown (struct glfs *fs, const char *path, uid_t uid, gid_t gid)
{
	int              ret = -1;
	int              valid = 0;
	struct iatt      iatt = {0, };

	iatt.ia_uid = uid;
	iatt.ia_gid = gid;
	valid = GF_SET_ATTR_UID|GF_SET_ATTR_GID;

	ret = glfs_setattr (fs, path, &iatt, valid, 1);

	return ret;
}


int
glfs_lchown (struct glfs *fs, const char *path, uid_t uid, gid_t gid)
{
	int              ret = -1;
	int              valid = 0;
	struct iatt      iatt = {0, };

	iatt.ia_uid = uid;
	iatt.ia_gid = gid;
	valid = GF_SET_ATTR_UID|GF_SET_ATTR_GID;

	ret = glfs_setattr (fs, path, &iatt, valid, 0);

	return ret;
}


int
glfs_fchown (struct glfs_fd *glfd, uid_t uid, gid_t gid)
{
	int              ret = -1;
	int              valid = 0;
	struct iatt      iatt = {0, };

	iatt.ia_uid = uid;
	iatt.ia_gid = gid;
	valid = GF_SET_ATTR_UID|GF_SET_ATTR_GID;

	ret = glfs_fsetattr (glfd, &iatt, valid);

	return ret;
}


int
glfs_utimens (struct glfs *fs, const char *path, struct timespec times[2])
{
	int              ret = -1;
	int              valid = 0;
	struct iatt      iatt = {0, };

	iatt.ia_atime = times[0].tv_sec;
	iatt.ia_atime_nsec = times[0].tv_nsec;
	iatt.ia_mtime = times[1].tv_sec;
	iatt.ia_mtime_nsec = times[1].tv_nsec;

	valid = GF_SET_ATTR_ATIME|GF_SET_ATTR_MTIME;

	ret = glfs_setattr (fs, path, &iatt, valid, 1);

	return ret;
}


int
glfs_lutimens (struct glfs *fs, const char *path, struct timespec times[2])
{
	int              ret = -1;
	int              valid = 0;
	struct iatt      iatt = {0, };

	iatt.ia_atime = times[0].tv_sec;
	iatt.ia_atime_nsec = times[0].tv_nsec;
	iatt.ia_mtime = times[1].tv_sec;
	iatt.ia_mtime_nsec = times[1].tv_nsec;

	valid = GF_SET_ATTR_ATIME|GF_SET_ATTR_MTIME;

	ret = glfs_setattr (fs, path, &iatt, valid, 0);

	return ret;
}


int
glfs_futimens (struct glfs_fd *glfd, struct timespec times[2])
{
	int              ret = -1;
	int              valid = 0;
	struct iatt      iatt = {0, };

	iatt.ia_atime = times[0].tv_sec;
	iatt.ia_atime_nsec = times[0].tv_nsec;
	iatt.ia_mtime = times[1].tv_sec;
	iatt.ia_mtime_nsec = times[1].tv_nsec;

	valid = GF_SET_ATTR_ATIME|GF_SET_ATTR_MTIME;

	ret = glfs_fsetattr (glfd, &iatt, valid);

	return ret;
}


int
glfs_getxattr_process (void *value, size_t size, dict_t *xattr,
		       const char *name)
{
	data_t *data = NULL;
	int     ret = -1;

	data = dict_get (xattr, (char *)name);
	if (!data) {
		errno = ENODATA;
		ret = -1;
		goto out;
	}

	ret = data->len;
	if (!value || !size)
		goto out;

	if (size < ret) {
		ret = -1;
		errno = ERANGE;
		goto out;
	}

	memcpy (value, data->data, ret);
out:
	if (xattr)
		dict_unref (xattr);
	return ret;
}


ssize_t
glfs_getxattr_common (struct glfs *fs, const char *path, const char *name,
		      void *value, size_t size, int follow)
{
	int              ret = -1;
	xlator_t        *subvol = NULL;
	loc_t            loc = {0, };
	struct iatt      iatt = {0, };
	dict_t          *xattr = NULL;
	int              reval = 0;

	__glfs_entry_fs (fs);

	subvol = glfs_active_subvol (fs);
	if (!subvol) {
		ret = -1;
		errno = EIO;
		goto out;
	}
retry:
	if (follow)
		ret = glfs_resolve (fs, subvol, path, &loc, &iatt, reval);
	else
		ret = glfs_lresolve (fs, subvol, path, &loc, &iatt, reval);

	ESTALE_RETRY (ret, errno, reval, &loc, retry);

	if (ret)
		goto out;

	ret = syncop_getxattr (subvol, &loc, &xattr, name);
        DECODE_SYNCOP_ERR (ret);

	ESTALE_RETRY (ret, errno, reval, &loc, retry);

	if (ret)
		goto out;

	ret = glfs_getxattr_process (value, size, xattr, name);
out:
	loc_wipe (&loc);

	glfs_subvol_done (fs, subvol);

	return ret;
}


ssize_t
glfs_getxattr (struct glfs *fs, const char *path, const char *name,
	       void *value, size_t size)
{
	return glfs_getxattr_common (fs, path, name, value, size, 1);
}


ssize_t
glfs_lgetxattr (struct glfs *fs, const char *path, const char *name,
		void *value, size_t size)
{
	return glfs_getxattr_common (fs, path, name, value, size, 0);
}


ssize_t
glfs_fgetxattr (struct glfs_fd *glfd, const char *name, void *value,
		size_t size)
{
	int              ret = -1;
	xlator_t        *subvol = NULL;
	dict_t          *xattr = NULL;
	fd_t            *fd = NULL;

	__glfs_entry_fd (glfd);

	subvol = glfs_active_subvol (glfd->fs);
	if (!subvol) {
		ret = -1;
		errno = EIO;
		goto out;
	}

	fd = glfs_resolve_fd (glfd->fs, subvol, glfd);
	if (!fd) {
		ret = -1;
		errno = EBADFD;
		goto out;
	}

	ret = syncop_fgetxattr (subvol, fd, &xattr, name);
        DECODE_SYNCOP_ERR (ret);
	if (ret)
		goto out;

	ret = glfs_getxattr_process (value, size, xattr, name);
out:
	if (fd)
		fd_unref (fd);

	glfs_subvol_done (glfd->fs, subvol);

	return ret;
}


int
glfs_listxattr_process (void *value, size_t size, dict_t *xattr)
{
	int ret = -1;

	if (!value || !size || !xattr)
		goto out;

	ret = dict_keys_join (NULL, 0, xattr, NULL);

	if (size < ret) {
		ret = -1;
		errno = ERANGE;
	} else {
		dict_keys_join (value, size, xattr, NULL);
	}

	dict_unref (xattr);

out:
	return ret;
}


ssize_t
glfs_listxattr_common (struct glfs *fs, const char *path, void *value,
		       size_t size, int follow)
{
	int              ret = -1;
	xlator_t        *subvol = NULL;
	loc_t            loc = {0, };
	struct iatt      iatt = {0, };
	dict_t          *xattr = NULL;
	int              reval = 0;

	__glfs_entry_fs (fs);

	subvol = glfs_active_subvol (fs);
	if (!subvol) {
		ret = -1;
		errno = EIO;
		goto out;
	}

retry:
	if (follow)
		ret = glfs_resolve (fs, subvol, path, &loc, &iatt, reval);
	else
		ret = glfs_lresolve (fs, subvol, path, &loc, &iatt, reval);

	ESTALE_RETRY (ret, errno, reval, &loc, retry);

	if (ret)
		goto out;

	ret = syncop_getxattr (subvol, &loc, &xattr, NULL);
        DECODE_SYNCOP_ERR (ret);

	ESTALE_RETRY (ret, errno, reval, &loc, retry);

	if (ret)
		goto out;

	ret = glfs_listxattr_process (value, size, xattr);
out:
	loc_wipe (&loc);

	glfs_subvol_done (fs, subvol);

	return ret;
}


ssize_t
glfs_listxattr (struct glfs *fs, const char *path, void *value, size_t size)
{
	return glfs_listxattr_common (fs, path, value, size, 1);
}


ssize_t
glfs_llistxattr (struct glfs *fs, const char *path, void *value, size_t size)
{
	return glfs_listxattr_common (fs, path, value, size, 0);
}


ssize_t
glfs_flistxattr (struct glfs_fd *glfd, void *value, size_t size)
{
	int              ret = -1;
	xlator_t        *subvol = NULL;
	dict_t          *xattr = NULL;
	fd_t            *fd = NULL;

	__glfs_entry_fd (glfd);

	subvol = glfs_active_subvol (glfd->fs);
	if (!subvol) {
		ret = -1;
		errno = EIO;
		goto out;
	}

	fd = glfs_resolve_fd (glfd->fs, subvol, glfd);
	if (!fd) {
		ret = -1;
		errno = EBADFD;
		goto out;
	}

	ret = syncop_fgetxattr (subvol, fd, &xattr, NULL);
        DECODE_SYNCOP_ERR (ret);
	if (ret)
		goto out;

	ret = glfs_listxattr_process (value, size, xattr);
out:
	if (fd)
		fd_unref (fd);

	glfs_subvol_done (glfd->fs, subvol);

	return ret;
}


dict_t *
dict_for_key_value (const char *name, const char *value, size_t size)
{
	dict_t *xattr = NULL;
	int     ret = 0;

	xattr = dict_new ();
	if (!xattr)
		return NULL;

	ret = dict_set_static_bin (xattr, (char *)name, (void *)value, size);
	if (ret) {
		dict_destroy (xattr);
		xattr = NULL;
	}

	return xattr;
}


int
glfs_setxattr_common (struct glfs *fs, const char *path, const char *name,
		      const void *value, size_t size, int flags, int follow)
{
	int              ret = -1;
	xlator_t        *subvol = NULL;
	loc_t            loc = {0, };
	struct iatt      iatt = {0, };
	dict_t          *xattr = NULL;
	int              reval = 0;

	__glfs_entry_fs (fs);

	subvol = glfs_active_subvol (fs);
	if (!subvol) {
		ret = -1;
		errno = EIO;
		goto out;
	}
retry:
	if (follow)
		ret = glfs_resolve (fs, subvol, path, &loc, &iatt, reval);
	else
		ret = glfs_lresolve (fs, subvol, path, &loc, &iatt, reval);

	ESTALE_RETRY (ret, errno, reval, &loc, retry);

	if (ret)
		goto out;

	xattr = dict_for_key_value (name, value, size);
	if (!xattr) {
		ret = -1;
		errno = ENOMEM;
		goto out;
	}

	ret = syncop_setxattr (subvol, &loc, xattr, flags);
        DECODE_SYNCOP_ERR (ret);

	ESTALE_RETRY (ret, errno, reval, &loc, retry);

out:
	loc_wipe (&loc);
	if (xattr)
		dict_unref (xattr);

	glfs_subvol_done (fs, subvol);

	return ret;
}


int
glfs_setxattr (struct glfs *fs, const char *path, const char *name,
	       const void *value, size_t size, int flags)
{
	return glfs_setxattr_common (fs, path, name, value, size, flags, 1);
}


int
glfs_lsetxattr (struct glfs *fs, const char *path, const char *name,
		const void *value, size_t size, int flags)
{
	return glfs_setxattr_common (fs, path, name, value, size, flags, 0);
}


int
glfs_fsetxattr (struct glfs_fd *glfd, const char *name, const void *value,
		size_t size, int flags)
{
	int              ret = -1;
	xlator_t        *subvol = NULL;
	dict_t          *xattr = NULL;
	fd_t            *fd = NULL;

	__glfs_entry_fd (glfd);

	subvol = glfs_active_subvol (glfd->fs);
	if (!subvol) {
		ret = -1;
		errno = EIO;
		goto out;
	}

	fd = glfs_resolve_fd (glfd->fs, subvol, glfd);
	if (!fd) {
		ret = -1;
		errno = EBADFD;
		goto out;
	}

	xattr = dict_for_key_value (name, value, size);
	if (!xattr) {
		ret = -1;
		errno = ENOMEM;
		goto out;
	}

	ret = syncop_fsetxattr (subvol, fd, xattr, flags);
        DECODE_SYNCOP_ERR (ret);
out:
	if (xattr)
		dict_unref (xattr);

	if (fd)
		fd_unref (fd);

	glfs_subvol_done (glfd->fs, subvol);

	return ret;
}


int
glfs_removexattr_common (struct glfs *fs, const char *path, const char *name,
			 int follow)
{
	int              ret = -1;
	xlator_t        *subvol = NULL;
	loc_t            loc = {0, };
	struct iatt      iatt = {0, };
	int              reval = 0;

	__glfs_entry_fs (fs);

	subvol = glfs_active_subvol (fs);
	if (!subvol) {
		ret = -1;
		errno = EIO;
		goto out;
	}
retry:
	if (follow)
		ret = glfs_resolve (fs, subvol, path, &loc, &iatt, reval);
	else
		ret = glfs_lresolve (fs, subvol, path, &loc, &iatt, reval);

	ESTALE_RETRY (ret, errno, reval, &loc, retry);

	if (ret)
		goto out;

	ret = syncop_removexattr (subvol, &loc, name);
        DECODE_SYNCOP_ERR (ret);

	ESTALE_RETRY (ret, errno, reval, &loc, retry);

out:
	loc_wipe (&loc);

	glfs_subvol_done (fs, subvol);

	return ret;
}


int
glfs_removexattr (struct glfs *fs, const char *path, const char *name)
{
	return glfs_removexattr_common (fs, path, name, 1);
}


int
glfs_lremovexattr (struct glfs *fs, const char *path, const char *name)
{
	return glfs_removexattr_common (fs, path, name, 0);
}


int
glfs_fremovexattr (struct glfs_fd *glfd, const char *name)
{
	int              ret = -1;
	xlator_t        *subvol = NULL;
	fd_t            *fd = NULL;

	__glfs_entry_fd (glfd);

	subvol = glfs_active_subvol (glfd->fs);
	if (!subvol) {
		ret = -1;
		errno = EIO;
		goto out;
	}

	fd = glfs_resolve_fd (glfd->fs, subvol, glfd);
	if (!fd) {
		ret = -1;
		errno = EBADFD;
		goto out;
	}

	ret = syncop_fremovexattr (subvol, fd, name);
        DECODE_SYNCOP_ERR (ret);
out:
	if (fd)
		fd_unref (fd);

	glfs_subvol_done (glfd->fs, subvol);

	return ret;
}


int
glfs_fallocate (struct glfs_fd *glfd, int keep_size, off_t offset, size_t len)
{
	int              ret = -1;
	xlator_t        *subvol = NULL;
	fd_t		*fd = NULL;

	__glfs_entry_fd (glfd);

	subvol = glfs_active_subvol (glfd->fs);
	if (!subvol) {
		ret = -1;
		errno = EIO;
		goto out;
	}

	fd = glfs_resolve_fd (glfd->fs, subvol, glfd);
	if (!fd) {
		ret = -1;
		errno = EBADFD;
		goto out;
	}

	ret = syncop_fallocate (subvol, fd, keep_size, offset, len);
        DECODE_SYNCOP_ERR (ret);
out:
	if (fd)
		fd_unref(fd);

	glfs_subvol_done (glfd->fs, subvol);

	return ret;
}


int
glfs_discard (struct glfs_fd *glfd, off_t offset, size_t len)
{
	int              ret = -1;
	xlator_t        *subvol = NULL;
	fd_t		*fd = NULL;

	__glfs_entry_fd (glfd);

	subvol = glfs_active_subvol (glfd->fs);
	if (!subvol) {
		ret = -1;
		errno = EIO;
		goto out;
	}

	fd = glfs_resolve_fd (glfd->fs, subvol, glfd);
	if (!fd) {
		ret = -1;
		errno = EBADFD;
		goto out;
	}

	ret = syncop_discard (subvol, fd, offset, len);
        DECODE_SYNCOP_ERR (ret);
out:
	if (fd)
		fd_unref(fd);

	glfs_subvol_done (glfd->fs, subvol);

	return ret;
}

int
glfs_zerofill (struct glfs_fd *glfd, off_t offset, off_t len)
{
        int               ret             = -1;
        xlator_t         *subvol          = NULL;
        fd_t             *fd              = NULL;

        __glfs_entry_fd (glfd);

        subvol = glfs_active_subvol (glfd->fs);
        if (!subvol) {
                errno = EIO;
                goto out;
        }

        fd = glfs_resolve_fd (glfd->fs, subvol, glfd);
        if (!fd) {
                errno = EBADFD;
                goto out;
        }

        ret = syncop_zerofill (subvol, fd, offset, len);
        DECODE_SYNCOP_ERR (ret);
out:
        if (fd)
                fd_unref(fd);

        glfs_subvol_done (glfd->fs, subvol);

        return ret;
}

int
glfs_chdir (struct glfs *fs, const char *path)
{
	int              ret = -1;
	xlator_t        *subvol = NULL;
	loc_t            loc = {0, };
	struct iatt      iatt = {0, };
	int              reval = 0;

	__glfs_entry_fs (fs);

	subvol = glfs_active_subvol (fs);
	if (!subvol) {
		ret = -1;
		errno = EIO;
		goto out;
	}
retry:
	ret = glfs_resolve (fs, subvol, path, &loc, &iatt, reval);

	ESTALE_RETRY (ret, errno, reval, &loc, retry);

	if (ret)
		goto out;

	if (!IA_ISDIR (iatt.ia_type)) {
		ret = -1;
		errno = ENOTDIR;
		goto out;
	}

	glfs_cwd_set (fs, loc.inode);

out:
	loc_wipe (&loc);

	glfs_subvol_done (fs, subvol);

	return ret;
}


int
glfs_fchdir (struct glfs_fd *glfd)
{
	int       ret = -1;
	inode_t  *inode = NULL;
	xlator_t *subvol = NULL;
	fd_t     *fd = NULL;

	__glfs_entry_fd (glfd);

	subvol = glfs_active_subvol (glfd->fs);
	if (!subvol) {
		ret = -1;
		errno = EIO;
		goto out;
	}

	fd = glfs_resolve_fd (glfd->fs, subvol, glfd);
	if (!fd) {
		ret = -1;
		errno = EBADFD;
		goto out;
	}

	inode = fd->inode;

	if (!IA_ISDIR (inode->ia_type)) {
		ret = -1;
		errno = ENOTDIR;
		goto out;
	}

	glfs_cwd_set (glfd->fs, inode);
	ret = 0;
out:
	if (fd)
		fd_unref (fd);

	glfs_subvol_done (glfd->fs, subvol);

	return ret;
}


char *
glfs_realpath (struct glfs *fs, const char *path, char *resolved_path)
{
	int              ret = -1;
	char            *retpath = NULL;
	char            *allocpath = NULL;
	xlator_t        *subvol = NULL;
	loc_t            loc = {0, };
	struct iatt      iatt = {0, };
	int              reval = 0;

	__glfs_entry_fs (fs);

	if (resolved_path)
		retpath = resolved_path;
	else
		retpath = allocpath = malloc (PATH_MAX + 1);

	if (!retpath) {
		ret = -1;
		errno = ENOMEM;
		goto out;
	}

	subvol = glfs_active_subvol (fs);
	if (!subvol) {
		ret = -1;
		errno = EIO;
		goto out;
	}
retry:
	ret = glfs_resolve (fs, subvol, path, &loc, &iatt, reval);

	ESTALE_RETRY (ret, errno, reval, &loc, retry);

	if (ret)
		goto out;

	if (loc.path) {
		strncpy (retpath, loc.path, PATH_MAX);
		retpath[PATH_MAX] = 0;
	}

out:
	loc_wipe (&loc);

	if (ret == -1) {
		if (allocpath)
			free (allocpath);
		retpath = NULL;
	}

	glfs_subvol_done (fs, subvol);

	return retpath;
}


char *
glfs_getcwd (struct glfs *fs, char *buf, size_t n)
{
	int              ret = -1;
	inode_t         *inode = NULL;
	char            *path = NULL;

	__glfs_entry_fs (fs);

	if (!buf || n < 2) {
		ret = -1;
		errno = EINVAL;
		goto out;
	}

	inode = glfs_cwd_get (fs);

	if (!inode) {
		strncpy (buf, "/", n);
		ret = 0;
		goto out;
	}

	ret = inode_path (inode, 0, &path);
	if (n <= ret) {
		ret = -1;
		errno = ERANGE;
		goto out;
	}

	strncpy (buf, path, n);
	ret = 0;
out:
	GF_FREE (path);

	if (inode)
		inode_unref (inode);

	if (ret < 0)
		return NULL;

	return buf;
}


static void
gf_flock_to_flock (struct gf_flock *gf_flock, struct flock *flock)
{
	flock->l_type   = gf_flock->l_type;
	flock->l_whence = gf_flock->l_whence;
	flock->l_start  = gf_flock->l_start;
	flock->l_len    = gf_flock->l_len;
	flock->l_pid    = gf_flock->l_pid;
}


static void
gf_flock_from_flock (struct gf_flock *gf_flock, struct flock *flock)
{
	gf_flock->l_type   = flock->l_type;
	gf_flock->l_whence = flock->l_whence;
	gf_flock->l_start  = flock->l_start;
	gf_flock->l_len    = flock->l_len;
	gf_flock->l_pid    = flock->l_pid;
}


int
glfs_posix_lock (struct glfs_fd *glfd, int cmd, struct flock *flock)
{
	int              ret = -1;
	xlator_t        *subvol = NULL;
	struct gf_flock  gf_flock = {0, };
	struct gf_flock  saved_flock = {0, };
	fd_t            *fd = NULL;

	__glfs_entry_fd (glfd);

	subvol = glfs_active_subvol (glfd->fs);
	if (!subvol) {
		ret = -1;
		errno = EIO;
		goto out;
	}

	fd = glfs_resolve_fd (glfd->fs, subvol, glfd);
	if (!fd) {
		ret = -1;
		errno = EBADFD;
		goto out;
	}

	gf_flock_from_flock (&gf_flock, flock);
	gf_flock_from_flock (&saved_flock, flock);
	ret = syncop_lk (subvol, fd, cmd, &gf_flock);
        DECODE_SYNCOP_ERR (ret);
	gf_flock_to_flock (&gf_flock, flock);

	if (ret == 0 && (cmd == F_SETLK || cmd == F_SETLKW))
		fd_lk_insert_and_merge (fd, cmd, &saved_flock);
out:
	if (fd)
		fd_unref (fd);

	glfs_subvol_done (glfd->fs, subvol);

	return ret;
}


struct glfs_fd *
glfs_dup (struct glfs_fd *glfd)
{
	xlator_t  *subvol = NULL;
	fd_t      *fd = NULL;
	glfs_fd_t *dupfd = NULL;
	struct glfs *fs = NULL;

	__glfs_entry_fd (glfd);

	fs = glfd->fs;
	subvol = glfs_active_subvol (fs);
	if (!subvol) {
		errno = EIO;
		goto out;
	}

	fd = glfs_resolve_fd (fs, subvol, glfd);
	if (!fd) {
		errno = EBADFD;
		goto out;
	}

	dupfd = glfs_fd_new (fs);
	if (!dupfd) {
		errno = ENOMEM;
		goto out;
	}

	dupfd->fd = fd_ref (fd);
out:
	if (fd)
		fd_unref (fd);
	if (dupfd)
		glfs_fd_bind (dupfd);

	glfs_subvol_done (fs, subvol);

	return dupfd;
}
