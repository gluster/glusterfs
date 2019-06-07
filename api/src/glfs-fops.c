
/*
  Copyright (c) 2012 Red Hat, Inc. <http://www.redhat.com>
  This file is part of GlusterFS.

  This file is licensed to you under your choice of the GNU Lesser
  General Public License, version 3 or any later version (LGPLv3 or
  later), or the GNU General Public License, version 2 (GPLv2), in all
  cases as published by the Free Software Foundation.
*/

/* for SEEK_HOLE and SEEK_DATA */
#ifndef _GNU_SOURCE
#define _GNU_SOURCE
#endif

#include <unistd.h>

#include "glfs-internal.h"
#include "glfs-mem-types.h"
#include "syncop.h"
#include "glfs.h"
#include "gfapi-messages.h"
#include "compat-errno.h"
#include <limits.h>
#include "glusterfs3.h"

#ifdef NAME_MAX
#define GF_NAME_MAX NAME_MAX
#else
#define GF_NAME_MAX 255
#endif

struct upcall_syncop_args {
    struct glfs *fs;
    struct glfs_upcall *up_arg;
};

#define READDIRBUF_SIZE (sizeof(struct dirent) + GF_NAME_MAX + 1)

typedef void (*glfs_io_cbk34) (glfs_fd_t *fd, ssize_t ret, void *data);

/*
 * This function will mark glfd for deletion and decrement its refcount.
 */
int
glfs_mark_glfd_for_deletion (struct glfs_fd *glfd)
{
        LOCK (&glfd->lock);
        {
                glfd->state = GLFD_CLOSE;
        }
        UNLOCK (&glfd->lock);

        GF_REF_PUT (glfd);

        return 0;
}

/* This function is usefull for all async fops. There is chance that glfd is
 * closed before async fop is completed. When glfd is closed we change the
 * state to GLFD_CLOSE.
 *
 * This function will return _gf_true if the glfd is still valid else return
 * _gf_false.
 */
gf_boolean_t
glfs_is_glfd_still_valid (struct glfs_fd *glfd)
{
        gf_boolean_t ret = _gf_false;

        LOCK (&glfd->lock);
        {
                if (glfd->state != GLFD_CLOSE)
                        ret = _gf_true;
        }
        UNLOCK (&glfd->lock);

        return ret;
}

void
glfd_set_state_bind (struct glfs_fd *glfd)
{
        LOCK (&glfd->lock);
        {
                glfd->state = GLFD_OPEN;
        }
        UNLOCK (&glfd->lock);

        fd_bind (glfd->fd);
        glfs_fd_bind (glfd);

        return;
}

/*
 * This routine is called when an upcall event of type
 * 'GF_UPCALL_CACHE_INVALIDATION' is received.
 * It makes a copy of the contents of the upcall cache-invalidation
 * data received into an entry which is stored in the upcall list
 * maintained by gfapi.
 */
int
glfs_get_upcall_cache_invalidation (struct gf_upcall *to_up_data,
                                    struct gf_upcall *from_up_data)
{

        struct gf_upcall_cache_invalidation *ca_data = NULL;
        struct gf_upcall_cache_invalidation *f_ca_data = NULL;
        int                                 ret      = -1;

        GF_VALIDATE_OR_GOTO (THIS->name, to_up_data, out);
        GF_VALIDATE_OR_GOTO (THIS->name, from_up_data, out);

        f_ca_data = from_up_data->data;
        GF_VALIDATE_OR_GOTO (THIS->name, f_ca_data, out);

        ca_data = GF_CALLOC (1, sizeof(*ca_data),
                            glfs_mt_upcall_entry_t);

        if (!ca_data) {
                gf_msg (THIS->name, GF_LOG_ERROR, errno,
                        API_MSG_ALLOC_FAILED,
                        "Upcall entry allocation failed.");
                goto out;
        }

        to_up_data->data = ca_data;

        ca_data->flags      = f_ca_data->flags;
        ca_data->expire_time_attr = f_ca_data->expire_time_attr;
        ca_data->stat       = f_ca_data->stat;
        ca_data->p_stat     = f_ca_data->p_stat;
        ca_data->oldp_stat  = f_ca_data->oldp_stat;

        ret = 0;
out:
        return ret;
}

int
glfs_get_upcall_lease (struct gf_upcall *to_up_data,
                             struct gf_upcall *from_up_data)
{

        struct gf_upcall_recall_lease *ca_data = NULL;
        struct gf_upcall_recall_lease *f_ca_data = NULL;
        int                                 ret      = -1;

        GF_VALIDATE_OR_GOTO (THIS->name, to_up_data, out);
        GF_VALIDATE_OR_GOTO (THIS->name, from_up_data, out);

        f_ca_data = from_up_data->data;
        GF_VALIDATE_OR_GOTO (THIS->name, f_ca_data, out);

        ca_data = GF_CALLOC (1, sizeof(*ca_data),
                            glfs_mt_upcall_entry_t);

        if (!ca_data) {
                gf_msg (THIS->name, GF_LOG_ERROR, errno,
                        API_MSG_ALLOC_FAILED,
                        "Upcall entry allocation failed.");
                goto out;
        }

        to_up_data->data = ca_data;

        ca_data->lease_type   = f_ca_data->lease_type;
        gf_uuid_copy (ca_data->tid, f_ca_data->tid);
        ca_data->dict       = f_ca_data->dict;

        ret = 0;
out:
        return ret;
}
int
glfs_loc_link (loc_t *loc, struct iatt *iatt)
{
	int ret = -1;
        inode_t *old_inode = NULL;
        uint64_t ctx_value = LOOKUP_NOT_NEEDED;

	if (!loc->inode) {
		errno = EINVAL;
		return -1;
	}

        old_inode = loc->inode;

        /* If the inode already exists in the cache, the inode
         * returned here points to the existing one. We need
         * to update loc.inode accordingly.
         */
	loc->inode = inode_link (loc->inode, loc->parent, loc->name, iatt);
	if (loc->inode) {
                inode_ctx_set (loc->inode, THIS, &ctx_value);
		inode_lookup (loc->inode);
                inode_unref (old_inode);
		ret = 0;
	} else {
		ret = -1;
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
pub_glfs_open (struct glfs *fs, const char *path, int flags)
{
	int              ret = -1;
	struct glfs_fd  *glfd = NULL;
	xlator_t        *subvol = NULL;
	loc_t            loc = {0, };
	struct iatt      iatt = {0, };
	int              reval = 0;
        dict_t          *fop_attr = NULL;

        DECLARE_OLD_THIS;
        __GLFS_ENTRY_VALIDATE_FS (fs, invalid_fs);

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
        glfd->fd->flags = flags;

        ret = get_fop_attr_thrd_key (&fop_attr);
        if (ret)
                gf_msg_debug ("gfapi", 0, "Getting leaseid from thread failed");

	ret = syncop_open (subvol, &loc, flags, glfd->fd, fop_attr, NULL);
        DECODE_SYNCOP_ERR (ret);

	ESTALE_RETRY (ret, errno, reval, &loc, retry);
out:
	loc_wipe (&loc);

        if (fop_attr)
                dict_unref (fop_attr);

	if (ret && glfd) {
                GF_REF_PUT (glfd);
		glfd = NULL;
	} else if (glfd) {
                glfd_set_state_bind (glfd);
	}

	glfs_subvol_done (fs, subvol);

        __GLFS_EXIT_FS;

invalid_fs:
	return glfd;
}

GFAPI_SYMVER_PUBLIC_DEFAULT(glfs_open, 3.4.0);

int
pub_glfs_close (struct glfs_fd *glfd)
{
	xlator_t  *subvol = NULL;
	int        ret = -1;
	fd_t      *fd = NULL;
	struct glfs *fs = NULL;
        dict_t    *fop_attr = NULL;

        DECLARE_OLD_THIS;
	__GLFS_ENTRY_VALIDATE_FD (glfd, invalid_fs);

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

        if (glfd->lk_owner.len != 0) {
                ret = syncopctx_setfslkowner (&glfd->lk_owner);
                if (ret)
                        goto out;
        }
        ret = get_fop_attr_thrd_key (&fop_attr);
        if (ret)
                gf_msg_debug ("gfapi", 0, "Getting leaseid from thread failed");

	ret = syncop_flush (subvol, fd, fop_attr, NULL);
        DECODE_SYNCOP_ERR (ret);
out:
	fs = glfd->fs;

        if (fd)
                fd_unref (fd);
        if (fop_attr)
                dict_unref (fop_attr);


        glfs_mark_glfd_for_deletion (glfd);
	glfs_subvol_done (fs, subvol);

        __GLFS_EXIT_FS;

invalid_fs:
	return ret;
}

GFAPI_SYMVER_PUBLIC_DEFAULT(glfs_close, 3.4.0);


int
pub_glfs_lstat (struct glfs *fs, const char *path, struct stat *stat)
{
	int              ret = -1;
	xlator_t        *subvol = NULL;
	loc_t            loc = {0, };
	struct iatt      iatt = {0, };
	int              reval = 0;

        DECLARE_OLD_THIS;
        __GLFS_ENTRY_VALIDATE_FS (fs, invalid_fs);

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

        __GLFS_EXIT_FS;

invalid_fs:
	return ret;
}

GFAPI_SYMVER_PUBLIC_DEFAULT(glfs_lstat, 3.4.0);


int
pub_glfs_stat (struct glfs *fs, const char *path, struct stat *stat)
{
	int              ret = -1;
	xlator_t        *subvol = NULL;
	loc_t            loc = {0, };
	struct iatt      iatt = {0, };
	int              reval = 0;

        DECLARE_OLD_THIS;
        __GLFS_ENTRY_VALIDATE_FS (fs, invalid_fs);

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

        __GLFS_EXIT_FS;

invalid_fs:
	return ret;
}

GFAPI_SYMVER_PUBLIC_DEFAULT(glfs_stat, 3.4.0);


int
pub_glfs_fstat (struct glfs_fd *glfd, struct stat *stat)
{
	int              ret = -1;
	xlator_t        *subvol = NULL;
	struct iatt      iatt = {0, };
	fd_t            *fd = NULL;

        DECLARE_OLD_THIS;
	__GLFS_ENTRY_VALIDATE_FD (glfd, invalid_fs);

        GF_REF_GET (glfd);

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

	ret = syncop_fstat (subvol, fd, &iatt, NULL, NULL);
        DECODE_SYNCOP_ERR (ret);

	if (ret == 0 && stat)
		glfs_iatt_to_stat (glfd->fs, &iatt, stat);
out:
	if (fd)
		fd_unref (fd);
        if (glfd)
                GF_REF_PUT (glfd);

	glfs_subvol_done (glfd->fs, subvol);

        __GLFS_EXIT_FS;

invalid_fs:
	return ret;
}

GFAPI_SYMVER_PUBLIC_DEFAULT(glfs_fstat, 3.4.0);


struct glfs_fd *
pub_glfs_creat (struct glfs *fs, const char *path, int flags, mode_t mode)
{
	int              ret = -1;
	struct glfs_fd  *glfd = NULL;
	xlator_t        *subvol = NULL;
	loc_t            loc = {0, };
	struct iatt      iatt = {0, };
	uuid_t           gfid;
	dict_t          *xattr_req = NULL;
	int              reval = 0;

        DECLARE_OLD_THIS;
        __GLFS_ENTRY_VALIDATE_FS (fs, invalid_fs);

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

	gf_uuid_generate (gfid);
	ret = dict_set_gfuuid (xattr_req, "gfid-req", gfid, true);
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
        glfd->fd->flags = flags;

        if (get_fop_attr_thrd_key (&xattr_req))
                gf_msg_debug ("gfapi", 0, "Getting leaseid from thread failed");
	if (ret == 0) {
		ret = syncop_open (subvol, &loc, flags, glfd->fd, xattr_req, NULL);
                DECODE_SYNCOP_ERR (ret);
	} else {
		ret = syncop_create (subvol, &loc, flags, mode, glfd->fd,
				     &iatt, xattr_req, NULL);
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
                GF_REF_PUT (glfd);
		glfd = NULL;
	} else if (glfd) {
                glfd_set_state_bind (glfd);
	}

	glfs_subvol_done (fs, subvol);

        __GLFS_EXIT_FS;

invalid_fs:
	return glfd;
}

GFAPI_SYMVER_PUBLIC_DEFAULT(glfs_creat, 3.4.0);

#ifdef HAVE_SEEK_HOLE
static int
glfs_seek (struct glfs_fd *glfd, off_t offset, int whence)
{
        int               ret             = -1;
        xlator_t         *subvol          = NULL;
        fd_t             *fd              = NULL;
        gf_seek_what_t    what            = 0;
        off_t             off             = -1;

        switch (whence) {
        case SEEK_DATA:
                what = GF_SEEK_DATA;
                break;
        case SEEK_HOLE:
                what = GF_SEEK_HOLE;
                break;
        default:
                /* other SEEK_* do not make sense, all operations get an offset
                 * and the position in the fd is not tracked */
                errno = EINVAL;
                goto out;
        }

        subvol = glfs_active_subvol (glfd->fs);
        if (!subvol) {
                errno = EIO;
                goto out;
        }

        fd = glfs_resolve_fd (glfd->fs, subvol, glfd);
        if (!fd) {
                errno = EBADFD;
                goto done;
        }

        ret = syncop_seek (subvol, fd, offset, what, NULL, &off);
        DECODE_SYNCOP_ERR (ret);

        if (ret != -1)
                glfd->offset = off;

done:
        if (fd)
                fd_unref (fd);

        glfs_subvol_done (glfd->fs, subvol);

out:
        return ret;
}
#endif

off_t
pub_glfs_lseek (struct glfs_fd *glfd, off_t offset, int whence)
{
	struct stat sb  = {0, };
	int         ret = -1;
        off_t       off = -1;

        DECLARE_OLD_THIS;
	__GLFS_ENTRY_VALIDATE_FD (glfd, invalid_fs);

        GF_REF_GET (glfd);

	switch (whence) {
	case SEEK_SET:
		glfd->offset = offset;
                ret = 0;
		break;
	case SEEK_CUR:
		glfd->offset += offset;
                ret = 0;
		break;
	case SEEK_END:
		ret = pub_glfs_fstat (glfd, &sb);
		if (ret) {
			/* seek cannot fail :O */
			break;
		}
		glfd->offset = sb.st_size + offset;
		break;
#ifdef HAVE_SEEK_HOLE
        case SEEK_DATA:
        case SEEK_HOLE:
                ret = glfs_seek (glfd, offset, whence);
                break;
#endif
        default:
                errno = EINVAL;
	}

        if (glfd)
                GF_REF_PUT (glfd);

        __GLFS_EXIT_FS;

        if (ret != -1)
                off = glfd->offset;

        return off;

invalid_fs:
        return -1;
}

GFAPI_SYMVER_PUBLIC_DEFAULT(glfs_lseek, 3.4.0);


ssize_t
pub_glfs_preadv (struct glfs_fd *glfd, const struct iovec *iovec, int iovcnt,
                 off_t offset, int flags)
{
	xlator_t       *subvol = NULL;
	ssize_t         ret = -1;
	ssize_t         size = -1;
	struct iovec   *iov = NULL;
	int             cnt = 0;
	struct iobref  *iobref = NULL;
	fd_t           *fd = NULL;
        dict_t         *fop_attr = NULL;

        DECLARE_OLD_THIS;
	__GLFS_ENTRY_VALIDATE_FD (glfd, invalid_fs);

        GF_REF_GET (glfd);

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

        ret = get_fop_attr_thrd_key (&fop_attr);
        if (ret)
                gf_msg_debug ("gfapi", 0, "Getting leaseid from thread failed");

	ret = syncop_readv (subvol, fd, size, offset, 0, &iov, &cnt, &iobref,
                            fop_attr, NULL);

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
        if (glfd)
                GF_REF_PUT (glfd);
        if (fop_attr)
                dict_unref (fop_attr);

	glfs_subvol_done (glfd->fs, subvol);

        __GLFS_EXIT_FS;

invalid_fs:
	return ret;
}

GFAPI_SYMVER_PUBLIC_DEFAULT(glfs_preadv, 3.4.0);


ssize_t
pub_glfs_read (struct glfs_fd *glfd, void *buf, size_t count, int flags)
{
	struct iovec iov = {0, };
	ssize_t      ret = 0;

	iov.iov_base = buf;
	iov.iov_len = count;

	ret = pub_glfs_preadv (glfd, &iov, 1, glfd->offset, flags);

	return ret;
}

GFAPI_SYMVER_PUBLIC_DEFAULT(glfs_read, 3.4.0);


ssize_t
pub_glfs_pread (struct glfs_fd *glfd, void *buf, size_t count, off_t offset,
                int flags)
{
	struct iovec iov = {0, };
	ssize_t      ret = 0;

	iov.iov_base = buf;
	iov.iov_len = count;

	ret = pub_glfs_preadv (glfd, &iov, 1, offset, flags);

	return ret;
}

GFAPI_SYMVER_PUBLIC_DEFAULT(glfs_pread, 3.4.0);


ssize_t
pub_glfs_readv (struct glfs_fd *glfd, const struct iovec *iov, int count,
                int flags)
{
	ssize_t      ret = 0;

	ret = pub_glfs_preadv (glfd, iov, count, glfd->offset, flags);

	return ret;
}

GFAPI_SYMVER_PUBLIC_DEFAULT(glfs_readv, 3.4.0);


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
glfs_io_async_cbk (int op_ret, int op_errno, call_frame_t *frame,
                   void *cookie, struct iovec *iovec, int count)
{
        struct glfs_io *gio = NULL;
        xlator_t       *subvol = NULL;
        struct glfs    *fs = NULL;
        struct glfs_fd *glfd = NULL;
        int             ret  = -1;

        GF_VALIDATE_OR_GOTO ("gfapi", frame, inval);
        GF_VALIDATE_OR_GOTO ("gfapi", cookie, inval);

        gio = frame->local;
        frame->local = NULL;
        subvol = cookie;
        glfd = gio->glfd;
        fs = glfd->fs;

        if (!glfs_is_glfd_still_valid (glfd))
                goto err;

        if (op_ret <= 0) {
                goto out;
        } else if (gio->op == GF_FOP_READ) {
                if (!iovec) {
                        op_ret = -1;
                        op_errno = EINVAL;
                        goto out;
                }

                op_ret = iov_copy (gio->iov, gio->count, iovec, count);
                glfd->offset = gio->offset + op_ret;
        } else if (gio->op == GF_FOP_WRITE) {
                glfd->offset = gio->offset + gio->iov->iov_len;
        }

out:
        errno = op_errno;
        gio->fn (gio->glfd, op_ret, gio->data);

err:
        fd_unref (glfd->fd);
        /* Since the async operation is complete
         * release the ref taken during the start
         * of async operation
         */
        GF_REF_PUT (glfd);

        GF_FREE (gio->iov);
        GF_FREE (gio);
        STACK_DESTROY (frame->root);
        glfs_subvol_done (fs, subvol);

        ret = 0;
inval:
        return ret;
}

static int
glfs_preadv_async_cbk (call_frame_t *frame, void *cookie, xlator_t *this,
                       int op_ret, int op_errno, struct iovec *iovec,
                       int count, struct iatt *stbuf, struct iobref *iobref,
                       dict_t *xdata)
{
        glfs_io_async_cbk (op_ret, op_errno, frame, cookie, iovec, count);

	return 0;
}


int
pub_glfs_preadv_async (struct glfs_fd *glfd, const struct iovec *iovec,
                       int count, off_t offset, int flags, glfs_io_cbk fn,
                       void *data)
{
	struct glfs_io *gio = NULL;
	int             ret = 0;
	call_frame_t   *frame = NULL;
	xlator_t       *subvol = NULL;
	glfs_t         *fs = NULL;
	fd_t           *fd = NULL;
        dict_t         *fop_attr = NULL;

        DECLARE_OLD_THIS;
	__GLFS_ENTRY_VALIDATE_FD (glfd, invalid_fs);

        GF_REF_GET (glfd);

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

        ret = get_fop_attr_thrd_key (&fop_attr);
        if (ret)
                gf_msg_debug ("gfapi", 0, "Getting leaseid from thread failed");

	STACK_WIND_COOKIE (frame, glfs_preadv_async_cbk, subvol, subvol,
			   subvol->fops->readv, fd, iov_length (iovec, count),
			   offset, flags, fop_attr);

out:
        if (ret) {
                if (fd)
                        fd_unref (fd);
                if (glfd)
                        GF_REF_PUT (glfd);
                if (gio) {
                        GF_FREE (gio->iov);
                        GF_FREE (gio);
                }
                if (frame) {
                        STACK_DESTROY (frame->root);
                }
		glfs_subvol_done (fs, subvol);
	}
        if (fop_attr)
                dict_unref (fop_attr);

        __GLFS_EXIT_FS;

	return ret;

invalid_fs:
        return -1;
}

GFAPI_SYMVER_PUBLIC_DEFAULT(glfs_preadv_async, 3.4.0);


int
pub_glfs_read_async (struct glfs_fd *glfd, void *buf, size_t count, int flags,
                     glfs_io_cbk fn, void *data)
{
	struct iovec iov = {0, };
	ssize_t      ret = 0;

	iov.iov_base = buf;
	iov.iov_len = count;

	ret = pub_glfs_preadv_async (glfd, &iov, 1, glfd->offset, flags, fn, data);

	return ret;
}

GFAPI_SYMVER_PUBLIC_DEFAULT(glfs_read_async, 3.4.0);


int
pub_glfs_pread_async (struct glfs_fd *glfd, void *buf, size_t count,
                      off_t offset, int flags, glfs_io_cbk fn, void *data)
{
	struct iovec iov = {0, };
	ssize_t      ret = 0;

	iov.iov_base = buf;
	iov.iov_len = count;

	ret = pub_glfs_preadv_async (glfd, &iov, 1, offset, flags, fn, data);

	return ret;
}

GFAPI_SYMVER_PUBLIC_DEFAULT(glfs_pread_async, 3.4.0);


int
pub_glfs_readv_async (struct glfs_fd *glfd, const struct iovec *iov, int count,
                      int flags, glfs_io_cbk fn, void *data)
{
	ssize_t      ret = 0;

	ret = pub_glfs_preadv_async (glfd, iov, count, glfd->offset, flags,
				  fn, data);
	return ret;
}

GFAPI_SYMVER_PUBLIC_DEFAULT(glfs_readv_async, 3.4.0);


static int
glfs_buf_copy (xlator_t *subvol, const struct iovec *iovec_src, int iovcnt,
               struct iobref **iobref, struct iobuf **iobuf,
               struct iovec *iov_dst)
{
        size_t         size = -1;
        int            ret  = 0;

        size = iov_length (iovec_src, iovcnt);

        *iobuf = iobuf_get2 (subvol->ctx->iobuf_pool, size);
        if (!(*iobuf)) {
                ret = -1;
                errno = ENOMEM;
                goto out;
        }

        *iobref = iobref_new ();
        if (!(*iobref)) {
                iobuf_unref (*iobuf);
                errno = ENOMEM;
                ret = -1;
                goto out;
        }

        ret = iobref_add (*iobref, *iobuf);
        if (ret) {
                iobuf_unref (*iobuf);
                iobref_unref (*iobref);
                errno = ENOMEM;
                ret = -1;
                goto out;
        }

        iov_unload (iobuf_ptr (*iobuf), iovec_src, iovcnt);  /* FIXME!!! */

        iov_dst->iov_base = iobuf_ptr (*iobuf);
        iov_dst->iov_len = size;

out:
        return ret;
}


ssize_t
pub_glfs_pwritev (struct glfs_fd *glfd, const struct iovec *iovec, int iovcnt,
                  off_t offset, int flags)
{
	xlator_t       *subvol = NULL;
	int             ret = -1;
	struct iobref  *iobref = NULL;
	struct iobuf   *iobuf = NULL;
	struct iovec    iov = {0, };
	fd_t           *fd = NULL;
        dict_t         *fop_attr = NULL;

        DECLARE_OLD_THIS;
	__GLFS_ENTRY_VALIDATE_FD (glfd, invalid_fs);

        GF_REF_GET (glfd);

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

        ret = glfs_buf_copy (subvol, iovec, iovcnt, &iobref, &iobuf, &iov);
        if (ret)
                goto out;

        ret = get_fop_attr_thrd_key (&fop_attr);
        if (ret)
                gf_msg_debug ("gfapi", 0, "Getting leaseid from thread failed");

        ret = syncop_writev (subvol, fd, &iov, 1, offset, iobref, flags,
                             fop_attr, NULL);

        DECODE_SYNCOP_ERR (ret);

	if (ret <= 0)
		goto out;

	glfd->offset = (offset + iov.iov_len);

out:
        if (iobuf)
                iobuf_unref (iobuf);
        if (iobref)
                iobref_unref (iobref);
	if (fd)
		fd_unref (fd);
        if (glfd)
                GF_REF_PUT (glfd);
        if (fop_attr)
                dict_unref (fop_attr);

	glfs_subvol_done (glfd->fs, subvol);

        __GLFS_EXIT_FS;

invalid_fs:
	return ret;
}

GFAPI_SYMVER_PUBLIC_DEFAULT(glfs_pwritev, 3.4.0);


ssize_t
pub_glfs_write (struct glfs_fd *glfd, const void *buf, size_t count, int flags)
{
	struct iovec iov = {0, };
	ssize_t      ret = 0;

	iov.iov_base = (void *) buf;
	iov.iov_len = count;

	ret = pub_glfs_pwritev (glfd, &iov, 1, glfd->offset, flags);

	return ret;
}

GFAPI_SYMVER_PUBLIC_DEFAULT(glfs_write, 3.4.0);


ssize_t
pub_glfs_writev (struct glfs_fd *glfd, const struct iovec *iov, int count,
                 int flags)
{
	ssize_t      ret = 0;

	ret = pub_glfs_pwritev (glfd, iov, count, glfd->offset, flags);

	return ret;
}

GFAPI_SYMVER_PUBLIC_DEFAULT(glfs_writev, 3.4.0);


ssize_t
pub_glfs_pwrite (struct glfs_fd *glfd, const void *buf, size_t count,
                 off_t offset, int flags)
{
	struct iovec iov = {0, };
	ssize_t      ret = 0;

	iov.iov_base = (void *) buf;
	iov.iov_len = count;

	ret = pub_glfs_pwritev (glfd, &iov, 1, offset, flags);

	return ret;
}

GFAPI_SYMVER_PUBLIC_DEFAULT(glfs_pwrite, 3.4.0);


extern glfs_t *pub_glfs_from_glfd (glfs_fd_t *);


static int
glfs_pwritev_async_cbk (call_frame_t *frame, void *cookie, xlator_t *this,
                        int op_ret, int op_errno, struct iatt *prebuf,
                        struct iatt *postbuf, dict_t *xdata)
{
        glfs_io_async_cbk (op_ret, op_errno, frame, cookie, NULL, 0);

        return 0;
}

int
pub_glfs_pwritev_async (struct glfs_fd *glfd, const struct iovec *iovec,
                        int count, off_t offset, int flags, glfs_io_cbk fn,
                        void *data)
{
	struct glfs_io *gio = NULL;
	int             ret = -1;
        call_frame_t   *frame = NULL;
        xlator_t       *subvol = NULL;
        fd_t           *fd = NULL;
        struct iobref  *iobref = NULL;
        struct iobuf   *iobuf = NULL;
        dict_t         *fop_attr = NULL;

        DECLARE_OLD_THIS;
        __GLFS_ENTRY_VALIDATE_FD (glfd, invalid_fs);

        /* Need to take explicit ref so that the fd
         * is not destroyed before the fop is complete
         */
        GF_REF_GET (glfd);

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

        gio = GF_CALLOC (1, sizeof (*gio), glfs_mt_glfs_io_t);
        if (!gio) {
                errno = ENOMEM;
                goto out;
        }

        gio->op     = GF_FOP_WRITE;
        gio->glfd   = glfd;
        gio->offset = offset;
        gio->flags  = flags;
        gio->fn     = fn;
        gio->data   = data;
        gio->count  = 1;
        gio->iov = GF_CALLOC (gio->count, sizeof (*(gio->iov)),
                              gf_common_mt_iovec);
        if (!gio->iov) {
                errno = ENOMEM;
                goto out;
        }

        ret = glfs_buf_copy (subvol, iovec, count, &iobref, &iobuf, gio->iov);
        if (ret)
                goto out;

        frame = syncop_create_frame (THIS);
        if (!frame) {
                errno = ENOMEM;
                ret = -1;
                goto out;
        }

        frame->local = gio;

        ret = get_fop_attr_thrd_key (&fop_attr);
        if (ret)
                gf_msg_debug ("gfapi", 0, "Getting leaseid from thread failed");

        STACK_WIND_COOKIE (frame, glfs_pwritev_async_cbk, subvol, subvol,
                           subvol->fops->writev, fd, gio->iov,
                           gio->count, offset, flags, iobref, fop_attr);

        ret = 0;
out:
        if (ret) {
                if (fd)
                        fd_unref (fd);
                if (glfd)
                        GF_REF_PUT (glfd);
                GF_FREE (gio);
                /*
                 * If there is any error condition check after the frame
                 * creation, we have to destroy the frame root.
                 */
                glfs_subvol_done (glfd->fs, subvol);
        }
        if (fop_attr)
                dict_unref (fop_attr);

        if (iobuf)
                iobuf_unref (iobuf);
        if (iobref)
                iobref_unref (iobref);

        __GLFS_EXIT_FS;

invalid_fs:
	return ret;
}

GFAPI_SYMVER_PUBLIC_DEFAULT(glfs_pwritev_async, 3.4.0);


int
pub_glfs_write_async (struct glfs_fd *glfd, const void *buf, size_t count,
                      int flags, glfs_io_cbk fn, void *data)
{
	struct iovec iov = {0, };
	ssize_t      ret = 0;

	iov.iov_base = (void *) buf;
	iov.iov_len = count;

	ret = pub_glfs_pwritev_async (glfd, &iov, 1, glfd->offset, flags, fn, data);

	return ret;
}

GFAPI_SYMVER_PUBLIC_DEFAULT(glfs_write_async, 3.4.0);


int
pub_glfs_pwrite_async (struct glfs_fd *glfd, const void *buf, int count,
                       off_t offset, int flags, glfs_io_cbk fn, void *data)
{
	struct iovec iov = {0, };
	ssize_t      ret = 0;

	iov.iov_base = (void *) buf;
	iov.iov_len = count;

	ret = pub_glfs_pwritev_async (glfd, &iov, 1, offset, flags, fn, data);

	return ret;
}

GFAPI_SYMVER_PUBLIC_DEFAULT(glfs_pwrite_async, 3.4.0);


int
pub_glfs_writev_async (struct glfs_fd *glfd, const struct iovec *iov, int count,
                       int flags, glfs_io_cbk fn, void *data)
{
	ssize_t      ret = 0;

	ret = pub_glfs_pwritev_async (glfd, iov, count, glfd->offset, flags,
				   fn, data);
	return ret;
}

GFAPI_SYMVER_PUBLIC_DEFAULT(glfs_writev_async, 3.4.0);


int
pub_glfs_fsync (struct glfs_fd *glfd)
{
	int              ret = -1;
	xlator_t        *subvol = NULL;
	fd_t            *fd = NULL;
        dict_t          *fop_attr = NULL;

        DECLARE_OLD_THIS;
	__GLFS_ENTRY_VALIDATE_FD (glfd, invalid_fs);

        GF_REF_GET (glfd);

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

        ret = get_fop_attr_thrd_key (&fop_attr);
        if (ret)
                gf_msg_debug ("gfapi", 0, "Getting leaseid from thread failed");

	ret = syncop_fsync (subvol, fd, 0, fop_attr, NULL);

        DECODE_SYNCOP_ERR (ret);
out:
	if (fd)
		fd_unref (fd);
        if (glfd)
                GF_REF_PUT (glfd);
        if (fop_attr)
                dict_unref (fop_attr);

	glfs_subvol_done (glfd->fs, subvol);

        __GLFS_EXIT_FS;

invalid_fs:
	return ret;
}

GFAPI_SYMVER_PUBLIC_DEFAULT(glfs_fsync, 3.4.0);

static int
glfs_fsync_async_cbk (call_frame_t *frame, void *cookie,
                      xlator_t *this, int32_t op_ret,
                      int32_t op_errno, struct iatt *prebuf,
                      struct iatt *postbuf, dict_t *xdata)
{
        glfs_io_async_cbk (op_ret, op_errno, frame, cookie, NULL, 0);

        return 0;
}

static int
glfs_fsync_async_common (struct glfs_fd *glfd, glfs_io_cbk fn, void *data,
			 int dataonly)
{
	struct glfs_io *gio = NULL;
	int             ret = 0;
        call_frame_t   *frame = NULL;
        xlator_t       *subvol = NULL;
        fd_t           *fd = NULL;

        /* Need to take explicit ref so that the fd
         * is not destroyed before the fop is complete
         */
        GF_REF_GET (glfd);

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

        frame = syncop_create_frame (THIS);
        if (!frame) {
                ret = -1;
                errno = ENOMEM;
                goto out;
        }

        gio = GF_CALLOC (1, sizeof (*gio), glfs_mt_glfs_io_t);
        if (!gio) {
                errno = ENOMEM;
                ret = -1;
                goto out;
        }

	gio->op     = GF_FOP_FSYNC;
	gio->glfd   = glfd;
	gio->flags  = dataonly;
	gio->fn     = fn;
	gio->data   = data;

        frame->local = gio;

        STACK_WIND_COOKIE (frame, glfs_fsync_async_cbk, subvol, subvol,
                           subvol->fops->fsync, fd, dataonly, NULL);

out:
        if (ret) {
                if (fd)
                        fd_unref (fd);
                GF_REF_PUT (glfd);
                GF_FREE (gio);
                if (frame)
                        STACK_DESTROY (frame->root);
                glfs_subvol_done (glfd->fs, subvol);
        }

        return ret;
}


int
pub_glfs_fsync_async (struct glfs_fd *glfd, glfs_io_cbk fn, void *data)
{
        int ret = -1;

        DECLARE_OLD_THIS;
        __GLFS_ENTRY_VALIDATE_FD (glfd, invalid_fs);

        ret = glfs_fsync_async_common (glfd, fn, data, 0);

        __GLFS_EXIT_FS;

invalid_fs:
        return ret;
}

GFAPI_SYMVER_PUBLIC_DEFAULT(glfs_fsync_async, 3.4.0);


int
pub_glfs_fdatasync (struct glfs_fd *glfd)
{
	int              ret = -1;
	xlator_t        *subvol = NULL;
	fd_t            *fd = NULL;
        dict_t          *fop_attr = NULL;

        DECLARE_OLD_THIS;
	__GLFS_ENTRY_VALIDATE_FD (glfd, invalid_fs);

        GF_REF_GET (glfd);

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

        ret = get_fop_attr_thrd_key (&fop_attr);
        if (ret)
                gf_msg_debug ("gfapi", 0, "Getting leaseid from thread failed");

	ret = syncop_fsync (subvol, fd, 1, fop_attr, NULL);

        DECODE_SYNCOP_ERR (ret);
out:
	if (fd)
		fd_unref (fd);
        if (glfd)
                GF_REF_PUT (glfd);
        if (fop_attr)
                dict_unref (fop_attr);

	glfs_subvol_done (glfd->fs, subvol);

        __GLFS_EXIT_FS;

invalid_fs:
	return ret;
}

GFAPI_SYMVER_PUBLIC_DEFAULT(glfs_fdatasync, 3.4.0);


int
pub_glfs_fdatasync_async (struct glfs_fd *glfd, glfs_io_cbk fn, void *data)
{
        int ret = -1;

        DECLARE_OLD_THIS;
        __GLFS_ENTRY_VALIDATE_FD (glfd, invalid_fs);

	ret = glfs_fsync_async_common (glfd, fn, data, 1);

        __GLFS_EXIT_FS;

invalid_fs:
        return ret;
}

GFAPI_SYMVER_PUBLIC_DEFAULT(glfs_fdatasync_async, 3.4.0);


int
pub_glfs_ftruncate (struct glfs_fd *glfd, off_t offset)
{
	int              ret = -1;
	xlator_t        *subvol = NULL;
	fd_t            *fd = NULL;
        dict_t         *fop_attr = NULL;

        DECLARE_OLD_THIS;
        __GLFS_ENTRY_VALIDATE_FD (glfd, invalid_fs);

        GF_REF_GET (glfd);

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

        ret = get_fop_attr_thrd_key (&fop_attr);
        if (ret)
                gf_msg_debug ("gfapi", 0, "Getting leaseid from thread failed");

	ret = syncop_ftruncate (subvol, fd, offset, fop_attr, NULL);

        DECODE_SYNCOP_ERR (ret);
out:
	if (fd)
		fd_unref (fd);
        if (glfd)
                GF_REF_PUT (glfd);
        if (fop_attr)
                dict_unref (fop_attr);

	glfs_subvol_done (glfd->fs, subvol);

        __GLFS_EXIT_FS;

invalid_fs:
	return ret;
}

GFAPI_SYMVER_PUBLIC_DEFAULT(glfs_ftruncate, 3.4.0);

int
pub_glfs_truncate (struct glfs *fs, const char *path, off_t length)
{
        int              ret = -1;
        xlator_t        *subvol = NULL;
        loc_t            loc = {0, };
        struct iatt      iatt = {0, };
        int              reval = 0;

        DECLARE_OLD_THIS;
        __GLFS_ENTRY_VALIDATE_FS (fs, invalid_fs);

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

        ret = syncop_truncate (subvol, &loc, length, NULL, NULL);
        DECODE_SYNCOP_ERR (ret);

        ESTALE_RETRY (ret, errno, reval, &loc, retry);
out:
        loc_wipe (&loc);

        glfs_subvol_done (fs, subvol);

        __GLFS_EXIT_FS;

invalid_fs:
        return ret;
}

GFAPI_SYMVER_PUBLIC_DEFAULT(glfs_truncate, 3.7.15);


static int
glfs_ftruncate_async_cbk (call_frame_t *frame, void *cookie, xlator_t *this,
                          int32_t op_ret, int32_t op_errno,
                          struct iatt *prebuf, struct iatt *postbuf,
                          dict_t *xdata)
{
        glfs_io_async_cbk (op_ret, op_errno, frame, cookie, NULL, 0);

        return 0;
}

int
pub_glfs_ftruncate_async (struct glfs_fd *glfd, off_t offset, glfs_io_cbk fn,
                          void *data)
{
	struct glfs_io *gio = NULL;
	int             ret = -1;
        call_frame_t   *frame = NULL;
        xlator_t       *subvol = NULL;
        fd_t           *fd = NULL;
        dict_t         *fop_attr = NULL;

        DECLARE_OLD_THIS;
        __GLFS_ENTRY_VALIDATE_FD (glfd, invalid_fs);

        /* Need to take explicit ref so that the fd
         * is not destroyed before the fop is complete
         */
        GF_REF_GET (glfd);

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

        frame = syncop_create_frame (THIS);
        if (!frame) {
                errno = ENOMEM;
                goto out;
        }

	gio = GF_CALLOC (1, sizeof (*gio), glfs_mt_glfs_io_t);
	if (!gio) {
		errno = ENOMEM;
		goto out;
	}

	gio->op     = GF_FOP_FTRUNCATE;
	gio->glfd   = glfd;
	gio->offset = offset;
	gio->fn     = fn;
	gio->data   = data;

        frame->local = gio;

        ret = get_fop_attr_thrd_key (&fop_attr);
        if (ret)
                gf_msg_debug ("gfapi", 0, "Getting leaseid from thread failed");

        STACK_WIND_COOKIE (frame, glfs_ftruncate_async_cbk, subvol, subvol,
                           subvol->fops->ftruncate, fd, offset, fop_attr);

        ret = 0;

out:
        if (ret) {
                if (fd)
                        fd_unref (fd);
                if (glfd)
                        GF_REF_PUT (glfd);
                GF_FREE (gio);
                if (frame)
                        STACK_DESTROY (frame->root);
                glfs_subvol_done (glfd->fs, subvol);
        }
        if (fop_attr)
                dict_unref (fop_attr);

        __GLFS_EXIT_FS;

invalid_fs:
	return ret;
}

GFAPI_SYMVER_PUBLIC_DEFAULT(glfs_ftruncate_async, 3.4.0);


int
pub_glfs_access (struct glfs *fs, const char *path, int mode)
{
	int              ret = -1;
	xlator_t        *subvol = NULL;
	loc_t            loc = {0, };
	struct iatt      iatt = {0, };
	int              reval = 0;

        DECLARE_OLD_THIS;
        __GLFS_ENTRY_VALIDATE_FS (fs, invalid_fs);

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

	ret = syncop_access (subvol, &loc, mode, NULL, NULL);
        DECODE_SYNCOP_ERR (ret);

	ESTALE_RETRY (ret, errno, reval, &loc, retry);
out:
	loc_wipe (&loc);

	glfs_subvol_done (fs, subvol);

        __GLFS_EXIT_FS;

invalid_fs:
	return ret;
}

GFAPI_SYMVER_PUBLIC_DEFAULT(glfs_access, 3.4.0);


int
pub_glfs_symlink (struct glfs *fs, const char *data, const char *path)
{
	int              ret = -1;
	xlator_t        *subvol = NULL;
	loc_t            loc = {0, };
	struct iatt      iatt = {0, };
	uuid_t           gfid;
	dict_t          *xattr_req = NULL;
	int              reval = 0;

        DECLARE_OLD_THIS;
        __GLFS_ENTRY_VALIDATE_FS (fs, invalid_fs);

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

	gf_uuid_generate (gfid);
	ret = dict_set_gfuuid (xattr_req, "gfid-req", gfid, true);
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

	ret = syncop_symlink (subvol, &loc, data, &iatt, xattr_req, NULL);
        DECODE_SYNCOP_ERR (ret);

	ESTALE_RETRY (ret, errno, reval, &loc, retry);

	if (ret == 0)
		ret = glfs_loc_link (&loc, &iatt);
out:
	loc_wipe (&loc);

	if (xattr_req)
		dict_unref (xattr_req);

	glfs_subvol_done (fs, subvol);

        __GLFS_EXIT_FS;

invalid_fs:
	return ret;
}

GFAPI_SYMVER_PUBLIC_DEFAULT(glfs_symlink, 3.4.0);


int
pub_glfs_readlink (struct glfs *fs, const char *path, char *buf, size_t bufsiz)
{
	int              ret = -1;
	xlator_t        *subvol = NULL;
	loc_t            loc = {0, };
	struct iatt      iatt = {0, };
	int              reval = 0;
	char            *linkval = NULL;

        DECLARE_OLD_THIS;
        __GLFS_ENTRY_VALIDATE_FS (fs, invalid_fs);

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

	ret = syncop_readlink (subvol, &loc, &linkval, bufsiz, NULL, NULL);
        DECODE_SYNCOP_ERR (ret);
	if (ret > 0) {
		memcpy (buf, linkval, ret);
		GF_FREE (linkval);
	}

	ESTALE_RETRY (ret, errno, reval, &loc, retry);
out:
	loc_wipe (&loc);

	glfs_subvol_done (fs, subvol);

        __GLFS_EXIT_FS;

invalid_fs:
	return ret;
}

GFAPI_SYMVER_PUBLIC_DEFAULT(glfs_readlink, 3.4.0);


int
pub_glfs_mknod (struct glfs *fs, const char *path, mode_t mode, dev_t dev)
{
	int              ret = -1;
	xlator_t        *subvol = NULL;
	loc_t            loc = {0, };
	struct iatt      iatt = {0, };
	uuid_t           gfid;
	dict_t          *xattr_req = NULL;
	int              reval = 0;

        DECLARE_OLD_THIS;
        __GLFS_ENTRY_VALIDATE_FS (fs, invalid_fs);

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

	gf_uuid_generate (gfid);
	ret = dict_set_gfuuid (xattr_req, "gfid-req", gfid, true);
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

	ret = syncop_mknod (subvol, &loc, mode, dev, &iatt, xattr_req, NULL);
        DECODE_SYNCOP_ERR (ret);

	ESTALE_RETRY (ret, errno, reval, &loc, retry);

	if (ret == 0)
		ret = glfs_loc_link (&loc, &iatt);
out:
	loc_wipe (&loc);

	if (xattr_req)
		dict_unref (xattr_req);

	glfs_subvol_done (fs, subvol);

        __GLFS_EXIT_FS;

invalid_fs:
	return ret;
}

GFAPI_SYMVER_PUBLIC_DEFAULT(glfs_mknod, 3.4.0);


int
pub_glfs_mkdir (struct glfs *fs, const char *path, mode_t mode)
{
	int              ret = -1;
	xlator_t        *subvol = NULL;
	loc_t            loc = {0, };
	struct iatt      iatt = {0, };
	uuid_t           gfid;
	dict_t          *xattr_req = NULL;
	int              reval = 0;

        DECLARE_OLD_THIS;
        __GLFS_ENTRY_VALIDATE_FS (fs, invalid_fs);

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

	gf_uuid_generate (gfid);
	ret = dict_set_gfuuid (xattr_req, "gfid-req", gfid, true);
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

	ret = syncop_mkdir (subvol, &loc, mode, &iatt, xattr_req, NULL);
        DECODE_SYNCOP_ERR (ret);

	ESTALE_RETRY (ret, errno, reval, &loc, retry);

	if (ret == 0)
		ret = glfs_loc_link (&loc, &iatt);
out:
	loc_wipe (&loc);

	if (xattr_req)
		dict_unref (xattr_req);

	glfs_subvol_done (fs, subvol);

        __GLFS_EXIT_FS;

invalid_fs:
	return ret;
}

GFAPI_SYMVER_PUBLIC_DEFAULT(glfs_mkdir, 3.4.0);


int
pub_glfs_unlink (struct glfs *fs, const char *path)
{
	int              ret = -1;
	xlator_t        *subvol = NULL;
	loc_t            loc = {0, };
	struct iatt      iatt = {0, };
	int              reval = 0;

        DECLARE_OLD_THIS;
        __GLFS_ENTRY_VALIDATE_FS (fs, invalid_fs);

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

        /* TODO: Add leaseid */
	ret = syncop_unlink (subvol, &loc, NULL, NULL);
        DECODE_SYNCOP_ERR (ret);

	ESTALE_RETRY (ret, errno, reval, &loc, retry);

	if (ret == 0)
		ret = glfs_loc_unlink (&loc);
out:
	loc_wipe (&loc);

	glfs_subvol_done (fs, subvol);

        __GLFS_EXIT_FS;

invalid_fs:
	return ret;
}

GFAPI_SYMVER_PUBLIC_DEFAULT(glfs_unlink, 3.4.0);


int
pub_glfs_rmdir (struct glfs *fs, const char *path)
{
	int              ret = -1;
	xlator_t        *subvol = NULL;
	loc_t            loc = {0, };
	struct iatt      iatt = {0, };
	int              reval = 0;

        DECLARE_OLD_THIS;
        __GLFS_ENTRY_VALIDATE_FS (fs, invalid_fs);

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

	ret = syncop_rmdir (subvol, &loc, 0, NULL, NULL);
        DECODE_SYNCOP_ERR (ret);

	ESTALE_RETRY (ret, errno, reval, &loc, retry);

	if (ret == 0)
		ret = glfs_loc_unlink (&loc);
out:
	loc_wipe (&loc);

	glfs_subvol_done (fs, subvol);

        __GLFS_EXIT_FS;

invalid_fs:
	return ret;
}

GFAPI_SYMVER_PUBLIC_DEFAULT(glfs_rmdir, 3.4.0);


int
pub_glfs_rename (struct glfs *fs, const char *oldpath, const char *newpath)
{
	int              ret = -1;
	xlator_t        *subvol = NULL;
	loc_t            oldloc = {0, };
	loc_t            newloc = {0, };
	struct iatt      oldiatt = {0, };
	struct iatt      newiatt = {0, };
	int              reval = 0;

        DECLARE_OLD_THIS;
        __GLFS_ENTRY_VALIDATE_FS (fs, invalid_fs);

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

	/* TODO: - check if new or old is a prefix of the other, and fail EINVAL
         *       - Add leaseid */

	ret = syncop_rename (subvol, &oldloc, &newloc, NULL, NULL);
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

        __GLFS_EXIT_FS;

invalid_fs:
	return ret;
}

GFAPI_SYMVER_PUBLIC_DEFAULT(glfs_rename, 3.4.0);


int
pub_glfs_link (struct glfs *fs, const char *oldpath, const char *newpath)
{
	int              ret = -1;
	xlator_t        *subvol = NULL;
	loc_t            oldloc = {0, };
	loc_t            newloc = {0, };
	struct iatt      oldiatt = {0, };
	struct iatt      newiatt = {0, };
	int              reval = 0;

        DECLARE_OLD_THIS;
        __GLFS_ENTRY_VALIDATE_FS (fs, invalid_fs);

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

	ret = syncop_link (subvol, &oldloc, &newloc, &newiatt, NULL, NULL);
        DECODE_SYNCOP_ERR (ret);

	if (ret == -1 && errno == ESTALE) {
		loc_wipe (&oldloc);
		loc_wipe (&newloc);
		if (reval--)
			goto retry;
	}

	if (ret == 0)
		ret = glfs_loc_link (&newloc, &newiatt);
out:
	loc_wipe (&oldloc);
	loc_wipe (&newloc);

	glfs_subvol_done (fs, subvol);

        __GLFS_EXIT_FS;

invalid_fs:
	return ret;
}

GFAPI_SYMVER_PUBLIC_DEFAULT(glfs_link, 3.4.0);


struct glfs_fd *
pub_glfs_opendir (struct glfs *fs, const char *path)
{
	int              ret = -1;
	struct glfs_fd  *glfd = NULL;
	xlator_t        *subvol = NULL;
	loc_t            loc = {0, };
	struct iatt      iatt = {0, };
	int              reval = 0;

        DECLARE_OLD_THIS;
        __GLFS_ENTRY_VALIDATE_FS (fs, invalid_fs);

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

	ret = syncop_opendir (subvol, &loc, glfd->fd, NULL, NULL);
        DECODE_SYNCOP_ERR (ret);

	ESTALE_RETRY (ret, errno, reval, &loc, retry);
out:
	loc_wipe (&loc);

	if (ret && glfd) {
		GF_REF_PUT (glfd);
		glfd = NULL;
	} else if (glfd) {
                glfd_set_state_bind (glfd);
	}

	glfs_subvol_done (fs, subvol);

        __GLFS_EXIT_FS;

invalid_fs:
	return glfd;
}

GFAPI_SYMVER_PUBLIC_DEFAULT(glfs_opendir, 3.4.0);


int
pub_glfs_closedir (struct glfs_fd *glfd)
{
        int ret = -1;

        DECLARE_OLD_THIS;
	__GLFS_ENTRY_VALIDATE_FD (glfd, invalid_fs);

	gf_dirent_free (list_entry (&glfd->entries, gf_dirent_t, list));

        glfs_mark_glfd_for_deletion (glfd);

        __GLFS_EXIT_FS;

        ret = 0;

invalid_fs:
	return ret;
}

GFAPI_SYMVER_PUBLIC_DEFAULT(glfs_closedir, 3.4.0);


long
pub_glfs_telldir (struct glfs_fd *fd)
{
	return fd->offset;
}

GFAPI_SYMVER_PUBLIC_DEFAULT(glfs_telldir, 3.4.0);


void
pub_glfs_seekdir (struct glfs_fd *fd, long offset)
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

GFAPI_SYMVER_PUBLIC_DEFAULT(glfs_seekdir, 3.4.0);

static int
glfs_discard_async_cbk (call_frame_t *frame, void *cookie,
                        xlator_t *this, int32_t op_ret,
                        int32_t op_errno, struct iatt *preop_stbuf,
                        struct iatt *postop_stbuf, dict_t *xdata)
{
        glfs_io_async_cbk (op_ret, op_errno, frame, cookie, NULL, 0);

        return 0;
}

int
pub_glfs_discard_async (struct glfs_fd *glfd, off_t offset, size_t len,
                        glfs_io_cbk fn, void *data)
{
	struct glfs_io *gio = NULL;
	int             ret = -1;
        call_frame_t   *frame = NULL;
        xlator_t       *subvol = NULL;
        fd_t           *fd = NULL;
        dict_t         *fop_attr = NULL;

        DECLARE_OLD_THIS;
        __GLFS_ENTRY_VALIDATE_FD (glfd, invalid_fs);

        /* Need to take explicit ref so that the fd
         * is not destroyed before the fop is complete
         */
        GF_REF_GET (glfd);

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

        frame = syncop_create_frame (THIS);
        if (!frame) {
                errno = ENOMEM;
                goto out;
        }

	gio = GF_CALLOC (1, sizeof (*gio), glfs_mt_glfs_io_t);
	if (!gio) {
		errno = ENOMEM;
		goto out;
	}

	gio->op     = GF_FOP_DISCARD;
	gio->glfd   = glfd;
	gio->offset = offset;
	gio->count  = len;
	gio->fn     = fn;
	gio->data   = data;

        frame->local = gio;
        ret = get_fop_attr_thrd_key (&fop_attr);
        if (ret)
                gf_msg_debug ("gfapi", 0, "Getting leaseid from thread failed");

        STACK_WIND_COOKIE (frame, glfs_discard_async_cbk, subvol, subvol,
                           subvol->fops->discard, fd, offset, len, fop_attr);

        ret = 0;
out:
        if (ret) {
                if (fd)
                        fd_unref (fd);
                if (glfd)
                        GF_REF_PUT (glfd);
                GF_FREE (gio);
                if (frame)
                        STACK_DESTROY (frame->root);
                glfs_subvol_done (glfd->fs, subvol);
        }

        __GLFS_EXIT_FS;

invalid_fs:
	return ret;
}

GFAPI_SYMVER_PUBLIC_DEFAULT(glfs_discard_async, 3.5.0);


static int
glfs_zerofill_async_cbk (call_frame_t *frame, void *cookie,
                         xlator_t *this, int32_t op_ret,
                         int32_t op_errno, struct iatt *preop_stbuf,
                         struct iatt *postop_stbuf, dict_t *xdata)
{
        glfs_io_async_cbk (op_ret, op_errno, frame, cookie, NULL, 0);

        return 0;
}


int
pub_glfs_zerofill_async (struct glfs_fd *glfd, off_t offset, off_t len,
                         glfs_io_cbk fn, void *data)
{
        struct glfs_io *gio  = NULL;
        int             ret  = -1;
        call_frame_t   *frame = NULL;
        xlator_t       *subvol = NULL;
        fd_t           *fd = NULL;
        dict_t         *fop_attr = NULL;

        DECLARE_OLD_THIS;
        __GLFS_ENTRY_VALIDATE_FD (glfd, invalid_fs);

        /* Need to take explicit ref so that the fd
         * is not destroyed before the fop is complete
         */
        GF_REF_GET (glfd);

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

        frame = syncop_create_frame (THIS);
        if (!frame) {
                errno = ENOMEM;
                goto out;
        }

        gio = GF_CALLOC (1, sizeof (*gio), glfs_mt_glfs_io_t);
        if (!gio) {
                errno = ENOMEM;
                goto out;
        }

        gio->op     = GF_FOP_ZEROFILL;
        gio->glfd   = glfd;
        gio->offset = offset;
        gio->count  = len;
        gio->fn     = fn;
        gio->data   = data;

        frame->local = gio;

        ret = get_fop_attr_thrd_key (&fop_attr);
        if (ret)
                gf_msg_debug ("gfapi", 0, "Getting leaseid from thread failed");

        STACK_WIND_COOKIE (frame, glfs_zerofill_async_cbk, subvol, subvol,
                           subvol->fops->zerofill, fd, offset, len, fop_attr);
        ret = 0;
out:
        if (ret) {
                if (fd)
                        fd_unref (fd);
                if (glfd)
                        GF_REF_PUT (glfd);
                GF_FREE (gio);
                if (frame)
                        STACK_DESTROY (frame->root);
                glfs_subvol_done (glfd->fs, subvol);
        }
        if (fop_attr)
                dict_unref (fop_attr);

        __GLFS_EXIT_FS;

invalid_fs:
        return ret;
}

GFAPI_SYMVER_PUBLIC_DEFAULT(glfs_zerofill_async, 3.5.0);


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

	strncpy (dirent->d_name, gf_dirent->d_name, NAME_MAX);
	dirent->d_name[NAME_MAX] = 0;
}


int
glfd_entry_refresh (struct glfs_fd *glfd, int plus)
{
	xlator_t        *subvol = NULL;
	gf_dirent_t      entries;
	gf_dirent_t      old;
        gf_dirent_t     *entry = NULL;
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
				       &entries, NULL, NULL);
	else
		ret = syncop_readdir (subvol, fd, 131072, glfd->offset,
				      &entries, NULL, NULL);
        DECODE_SYNCOP_ERR (ret);
	if (ret >= 0) {
		if (plus) {
                        list_for_each_entry (entry, &entries.list, list) {
                                if (!entry->inode &&
                                    !IA_ISDIR (entry->d_stat.ia_type)) {
                                        /* entry->inode for directories will be
                                         * always set to null to force a lookup
                                         * on the dentry. Also we will have
                                         * proper stat if directory present on
                                         * hashed subvolume.
                                         */
                                        gf_fill_iatt_for_dirent (entry,
                                                                 fd->inode,
                                                                 subvol);
                                }
                        }

			gf_link_inodes_from_dirent (THIS, fd->inode, &entries);
                }

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


struct dirent *
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
pub_glfs_readdirplus_r (struct glfs_fd *glfd, struct stat *stat,
                        struct dirent *ext, struct dirent **res)
{
	int              ret = 0;
	gf_dirent_t     *entry = NULL;
	struct dirent   *buf = NULL;

        DECLARE_OLD_THIS;
	__GLFS_ENTRY_VALIDATE_FD (glfd, invalid_fs);

        GF_REF_GET (glfd);

	errno = 0;

	if (ext)
		buf = ext;
	else
		buf = glfs_readdirbuf_get (glfd);

	if (!buf) {
		errno = ENOMEM;
                ret = -1;
                goto out;
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

out:
        if (glfd)
                GF_REF_PUT (glfd);

        __GLFS_EXIT_FS;

	return ret;

invalid_fs:
        return -1;
}

GFAPI_SYMVER_PUBLIC_DEFAULT(glfs_readdirplus_r, 3.4.0);


int
pub_glfs_readdir_r (struct glfs_fd *glfd, struct dirent *buf,
                    struct dirent **res)
{
	return pub_glfs_readdirplus_r (glfd, 0, buf, res);
}

GFAPI_SYMVER_PUBLIC_DEFAULT(glfs_readdir_r, 3.4.0);


struct dirent *
pub_glfs_readdirplus (struct glfs_fd *glfd, struct stat *stat)
{
        struct dirent *res = NULL;
        int ret = -1;

        ret = pub_glfs_readdirplus_r (glfd, stat, NULL, &res);
        if (ret)
                return NULL;

        return res;
}

GFAPI_SYMVER_PUBLIC_DEFAULT(glfs_readdirplus, 3.5.0);


struct dirent *
pub_glfs_readdir (struct glfs_fd *glfd)
{
        return pub_glfs_readdirplus (glfd, NULL);
}

GFAPI_SYMVER_PUBLIC_DEFAULT(glfs_readdir, 3.5.0);


int
pub_glfs_statvfs (struct glfs *fs, const char *path, struct statvfs *buf)
{
	int              ret = -1;
	xlator_t        *subvol = NULL;
	loc_t            loc = {0, };
	struct iatt      iatt = {0, };
	int              reval = 0;

        DECLARE_OLD_THIS;
        __GLFS_ENTRY_VALIDATE_FS (fs, invalid_fs);

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

	ret = syncop_statfs (subvol, &loc, buf, NULL, NULL);
        DECODE_SYNCOP_ERR (ret);

	ESTALE_RETRY (ret, errno, reval, &loc, retry);
out:
	loc_wipe (&loc);

	glfs_subvol_done (fs, subvol);

        __GLFS_EXIT_FS;

invalid_fs:
	return ret;
}

GFAPI_SYMVER_PUBLIC_DEFAULT(glfs_statvfs, 3.4.0);


int
glfs_setattr (struct glfs *fs, const char *path, struct iatt *iatt,
	      int valid, int follow)
{
	int              ret = -1;
	xlator_t        *subvol = NULL;
	loc_t            loc = {0, };
	struct iatt      riatt = {0, };
	int              reval = 0;

        DECLARE_OLD_THIS;
        __GLFS_ENTRY_VALIDATE_FS (fs, invalid_fs);

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

        /* TODO : Add leaseid */
	ret = syncop_setattr (subvol, &loc, iatt, valid, 0, 0, NULL, NULL);
        DECODE_SYNCOP_ERR (ret);

	ESTALE_RETRY (ret, errno, reval, &loc, retry);
out:
	loc_wipe (&loc);

	glfs_subvol_done (fs, subvol);

        __GLFS_EXIT_FS;

invalid_fs:
	return ret;
}


int
glfs_fsetattr (struct glfs_fd *glfd, struct iatt *iatt, int valid)
{
	int              ret = -1;
	xlator_t        *subvol = NULL;
	fd_t            *fd = NULL;

        DECLARE_OLD_THIS;
	__GLFS_ENTRY_VALIDATE_FD (glfd, invalid_fs);

        GF_REF_GET (glfd);

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

        /* TODO : Add leaseid */
	ret = syncop_fsetattr (subvol, fd, iatt, valid, 0, 0, NULL, NULL);
        DECODE_SYNCOP_ERR (ret);
out:
	if (fd)
		fd_unref (fd);
        if (glfd)
                GF_REF_PUT (glfd);

	glfs_subvol_done (glfd->fs, subvol);

        __GLFS_EXIT_FS;

invalid_fs:
	return ret;
}


int
pub_glfs_chmod (struct glfs *fs, const char *path, mode_t mode)
{
	int              ret = -1;
	struct iatt      iatt = {0, };
	int              valid = 0;

	iatt.ia_prot = ia_prot_from_st_mode (mode);
	valid = GF_SET_ATTR_MODE;

	ret = glfs_setattr (fs, path, &iatt, valid, 1);

	return ret;
}

GFAPI_SYMVER_PUBLIC_DEFAULT(glfs_chmod, 3.4.0);


int
pub_glfs_fchmod (struct glfs_fd *glfd, mode_t mode)
{
	int              ret = -1;
	struct iatt      iatt = {0, };
	int              valid = 0;

	iatt.ia_prot = ia_prot_from_st_mode (mode);
	valid = GF_SET_ATTR_MODE;

	ret = glfs_fsetattr (glfd, &iatt, valid);

	return ret;
}

GFAPI_SYMVER_PUBLIC_DEFAULT(glfs_fchmod, 3.4.0);


int
pub_glfs_chown (struct glfs *fs, const char *path, uid_t uid, gid_t gid)
{
	int              ret = 0;
	int              valid = 0;
	struct iatt      iatt = {0, };

        if (uid != (uid_t) -1) {
                iatt.ia_uid = uid;
                valid = GF_SET_ATTR_UID;
        }

        if (gid != (uid_t) -1) {
                iatt.ia_gid = gid;
                valid = valid | GF_SET_ATTR_GID;
        }

        if (valid)
	        ret = glfs_setattr (fs, path, &iatt, valid, 1);

	return ret;
}

GFAPI_SYMVER_PUBLIC_DEFAULT(glfs_chown, 3.4.0);


int
pub_glfs_lchown (struct glfs *fs, const char *path, uid_t uid, gid_t gid)
{
	int              ret = 0;
	int              valid = 0;
	struct iatt      iatt = {0, };

        if (uid != (uid_t) -1) {
	        iatt.ia_uid = uid;
	        valid = GF_SET_ATTR_UID;
        }

        if (gid != (uid_t) -1) {
                iatt.ia_gid = gid;
                valid = valid | GF_SET_ATTR_GID;
        }

        if (valid)
	        ret = glfs_setattr (fs, path, &iatt, valid, 0);

	return ret;
}

GFAPI_SYMVER_PUBLIC_DEFAULT(glfs_lchown, 3.4.0);


int
pub_glfs_fchown (struct glfs_fd *glfd, uid_t uid, gid_t gid)
{
	int              ret = 0;
	int              valid = 0;
	struct iatt      iatt = {0, };

        if (uid != (uid_t) -1) {
                iatt.ia_uid = uid;
                valid = GF_SET_ATTR_UID;
        }

        if (gid != (uid_t) -1) {
                iatt.ia_gid = gid;
                valid = valid | GF_SET_ATTR_GID;
        }

        if (valid)
	        ret = glfs_fsetattr (glfd, &iatt, valid);

	return ret;
}

GFAPI_SYMVER_PUBLIC_DEFAULT(glfs_fchown, 3.4.0);


int
pub_glfs_utimens (struct glfs *fs, const char *path,
                  const struct timespec times[2])
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

GFAPI_SYMVER_PUBLIC_DEFAULT(glfs_utimens, 3.4.0);


int
pub_glfs_lutimens (struct glfs *fs, const char *path,
                   const struct timespec times[2])
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

GFAPI_SYMVER_PUBLIC_DEFAULT(glfs_lutimens, 3.4.0);


int
pub_glfs_futimens (struct glfs_fd *glfd, const struct timespec times[2])
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

GFAPI_SYMVER_PUBLIC_DEFAULT(glfs_futimens, 3.4.0);


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

        DECLARE_OLD_THIS;
        __GLFS_ENTRY_VALIDATE_FS (fs, invalid_fs);

        if (!name || *name == '\0') {
                ret = -1;
                errno = EINVAL;
                goto out;
        }

        if (strlen(name) > GF_XATTR_NAME_MAX) {
                ret = -1;
                errno = ENAMETOOLONG;
                goto out;
        }

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

	ret = syncop_getxattr (subvol, &loc, &xattr, name, NULL, NULL);
        DECODE_SYNCOP_ERR (ret);

	ESTALE_RETRY (ret, errno, reval, &loc, retry);

	if (ret)
		goto out;

	ret = glfs_getxattr_process (value, size, xattr, name);
out:
	loc_wipe (&loc);

        if (xattr)
                dict_unref (xattr);

	glfs_subvol_done (fs, subvol);

        __GLFS_EXIT_FS;

invalid_fs:
	return ret;
}


ssize_t
pub_glfs_getxattr (struct glfs *fs, const char *path, const char *name,
                   void *value, size_t size)
{
	return glfs_getxattr_common (fs, path, name, value, size, 1);
}

GFAPI_SYMVER_PUBLIC_DEFAULT(glfs_getxattr, 3.4.0);


ssize_t
pub_glfs_lgetxattr (struct glfs *fs, const char *path, const char *name,
                    void *value, size_t size)
{
	return glfs_getxattr_common (fs, path, name, value, size, 0);
}

GFAPI_SYMVER_PUBLIC_DEFAULT(glfs_lgetxattr, 3.4.0);


ssize_t
pub_glfs_fgetxattr (struct glfs_fd *glfd, const char *name, void *value,
                    size_t size)
{
	int              ret = -1;
	xlator_t        *subvol = NULL;
	dict_t          *xattr = NULL;
	fd_t            *fd = NULL;

        DECLARE_OLD_THIS;
	__GLFS_ENTRY_VALIDATE_FD (glfd, invalid_fs);

        GF_REF_GET (glfd);

        if (!name || *name == '\0') {
                ret = -1;
                errno = EINVAL;
                goto out;
        }

        if (strlen(name) > GF_XATTR_NAME_MAX) {
                ret = -1;
                errno = ENAMETOOLONG;
                goto out;
        }

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

	ret = syncop_fgetxattr (subvol, fd, &xattr, name, NULL, NULL);
        DECODE_SYNCOP_ERR (ret);
	if (ret)
		goto out;

	ret = glfs_getxattr_process (value, size, xattr, name);
out:
	if (fd)
		fd_unref (fd);
        if (glfd)
                GF_REF_PUT (glfd);
        if (xattr)
                dict_unref (xattr);

	glfs_subvol_done (glfd->fs, subvol);

        __GLFS_EXIT_FS;

invalid_fs:
	return ret;
}

GFAPI_SYMVER_PUBLIC_DEFAULT(glfs_fgetxattr, 3.4.0);


int
glfs_listxattr_process (void *value, size_t size, dict_t *xattr)
{
	int ret = -1;

	if (!xattr)
		goto out;

	ret = dict_keys_join (NULL, 0, xattr, NULL);

        if (!value || !size)
                goto out;

	if (size < ret) {
		ret = -1;
		errno = ERANGE;
	} else {
		dict_keys_join (value, size, xattr, NULL);
	}

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

        DECLARE_OLD_THIS;
        __GLFS_ENTRY_VALIDATE_FS (fs, invalid_fs);

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

	ret = syncop_getxattr (subvol, &loc, &xattr, NULL, NULL, NULL);
        DECODE_SYNCOP_ERR (ret);

	ESTALE_RETRY (ret, errno, reval, &loc, retry);

	if (ret)
		goto out;

	ret = glfs_listxattr_process (value, size, xattr);
out:
	loc_wipe (&loc);

        if (xattr)
                dict_unref (xattr);

	glfs_subvol_done (fs, subvol);

        __GLFS_EXIT_FS;

invalid_fs:
	return ret;
}


ssize_t
pub_glfs_listxattr (struct glfs *fs, const char *path, void *value, size_t size)
{
	return glfs_listxattr_common (fs, path, value, size, 1);
}

GFAPI_SYMVER_PUBLIC_DEFAULT(glfs_listxattr, 3.4.0);


ssize_t
pub_glfs_llistxattr (struct glfs *fs, const char *path, void *value, size_t size)
{
	return glfs_listxattr_common (fs, path, value, size, 0);
}

GFAPI_SYMVER_PUBLIC_DEFAULT(glfs_llistxattr, 3.4.0);


ssize_t
pub_glfs_flistxattr (struct glfs_fd *glfd, void *value, size_t size)
{
	int              ret = -1;
	xlator_t        *subvol = NULL;
	dict_t          *xattr = NULL;
	fd_t            *fd = NULL;

        DECLARE_OLD_THIS;
	__GLFS_ENTRY_VALIDATE_FD (glfd, invalid_fs);

        GF_REF_GET (glfd);

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

	ret = syncop_fgetxattr (subvol, fd, &xattr, NULL, NULL, NULL);
        DECODE_SYNCOP_ERR (ret);
	if (ret)
		goto out;

	ret = glfs_listxattr_process (value, size, xattr);
out:
	if (fd)
		fd_unref (fd);
        if (glfd)
                GF_REF_PUT (glfd);
        if (xattr)
                dict_unref (xattr);

	glfs_subvol_done (glfd->fs, subvol);

        __GLFS_EXIT_FS;

invalid_fs:
	return ret;
}

GFAPI_SYMVER_PUBLIC_DEFAULT(glfs_flistxattr, 3.4.0);

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
        void            *value_cp = NULL;

        DECLARE_OLD_THIS;
        __GLFS_ENTRY_VALIDATE_FS (fs, invalid_fs);

        if (!name || *name == '\0') {
                ret = -1;
                errno = EINVAL;
                goto out;
        }

        if (strlen(name) > GF_XATTR_NAME_MAX) {
                ret = -1;
                errno = ENAMETOOLONG;
                goto out;
        }

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

        value_cp = gf_memdup (value, size);
        GF_CHECK_ALLOC_AND_LOG (subvol->name, value_cp, ret, "Failed to"
                                " duplicate setxattr value", out);

	xattr = dict_for_key_value (name, value_cp, size, _gf_false);
	if (!xattr) {
                GF_FREE (value_cp);
		ret = -1;
		errno = ENOMEM;
		goto out;
	}

	ret = syncop_setxattr (subvol, &loc, xattr, flags, NULL, NULL);
        DECODE_SYNCOP_ERR (ret);

out:
	loc_wipe (&loc);
	if (xattr)
		dict_unref (xattr);

	glfs_subvol_done (fs, subvol);

        __GLFS_EXIT_FS;

invalid_fs:
	return ret;
}


int
pub_glfs_setxattr (struct glfs *fs, const char *path, const char *name,
                   const void *value, size_t size, int flags)
{
	return glfs_setxattr_common (fs, path, name, value, size, flags, 1);
}

GFAPI_SYMVER_PUBLIC_DEFAULT(glfs_setxattr, 3.4.0);


int
pub_glfs_lsetxattr (struct glfs *fs, const char *path, const char *name,
                    const void *value, size_t size, int flags)
{
	return glfs_setxattr_common (fs, path, name, value, size, flags, 0);
}

GFAPI_SYMVER_PUBLIC_DEFAULT(glfs_lsetxattr, 3.4.0);


int
pub_glfs_fsetxattr (struct glfs_fd *glfd, const char *name, const void *value,
                    size_t size, int flags)
{
	int              ret = -1;
	xlator_t        *subvol = NULL;
	dict_t          *xattr = NULL;
	fd_t            *fd = NULL;
        void            *value_cp = NULL;

        DECLARE_OLD_THIS;
	__GLFS_ENTRY_VALIDATE_FD (glfd, invalid_fs);

        GF_REF_GET (glfd);

        if (!name || *name == '\0') {
                ret = -1;
                errno = EINVAL;
                goto out;
        }

        if (strlen(name) > GF_XATTR_NAME_MAX) {
                ret = -1;
                errno = ENAMETOOLONG;
                goto out;
        }

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

        value_cp = gf_memdup (value, size);
        GF_CHECK_ALLOC_AND_LOG (subvol->name, value_cp, ret, "Failed to"
                                " duplicate setxattr value", out);

	xattr = dict_for_key_value (name, value_cp, size, _gf_false);
	if (!xattr) {
                GF_FREE (value_cp);
		ret = -1;
		errno = ENOMEM;
		goto out;
	}

	ret = syncop_fsetxattr (subvol, fd, xattr, flags, NULL, NULL);
        DECODE_SYNCOP_ERR (ret);
out:
	if (xattr)
		dict_unref (xattr);

	if (fd)
		fd_unref (fd);
        if (glfd)
                GF_REF_PUT (glfd);

	glfs_subvol_done (glfd->fs, subvol);

        __GLFS_EXIT_FS;

invalid_fs:
	return ret;
}

GFAPI_SYMVER_PUBLIC_DEFAULT(glfs_fsetxattr, 3.4.0);


int
glfs_removexattr_common (struct glfs *fs, const char *path, const char *name,
			 int follow)
{
	int              ret = -1;
	xlator_t        *subvol = NULL;
	loc_t            loc = {0, };
	struct iatt      iatt = {0, };
	int              reval = 0;

        DECLARE_OLD_THIS;
        __GLFS_ENTRY_VALIDATE_FS (fs, invalid_fs);

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

	ret = syncop_removexattr (subvol, &loc, name, NULL, NULL);
        DECODE_SYNCOP_ERR (ret);

	ESTALE_RETRY (ret, errno, reval, &loc, retry);

out:
	loc_wipe (&loc);

	glfs_subvol_done (fs, subvol);

        __GLFS_EXIT_FS;

invalid_fs:
	return ret;
}


int
pub_glfs_removexattr (struct glfs *fs, const char *path, const char *name)
{
	return glfs_removexattr_common (fs, path, name, 1);
}

GFAPI_SYMVER_PUBLIC_DEFAULT(glfs_removexattr, 3.4.0);


int
pub_glfs_lremovexattr (struct glfs *fs, const char *path, const char *name)
{
	return glfs_removexattr_common (fs, path, name, 0);
}

GFAPI_SYMVER_PUBLIC_DEFAULT(glfs_lremovexattr, 3.4.0);


int
pub_glfs_fremovexattr (struct glfs_fd *glfd, const char *name)
{
	int              ret = -1;
	xlator_t        *subvol = NULL;
	fd_t            *fd = NULL;

        DECLARE_OLD_THIS;
	__GLFS_ENTRY_VALIDATE_FD (glfd, invalid_fs);

        GF_REF_GET (glfd);

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

	ret = syncop_fremovexattr (subvol, fd, name, NULL, NULL);
        DECODE_SYNCOP_ERR (ret);
out:
	if (fd)
		fd_unref (fd);
        if (glfd)
                GF_REF_PUT (glfd);

	glfs_subvol_done (glfd->fs, subvol);

        __GLFS_EXIT_FS;

invalid_fs:
	return ret;
}

GFAPI_SYMVER_PUBLIC_DEFAULT(glfs_fremovexattr, 3.4.0);


int
pub_glfs_fallocate (struct glfs_fd *glfd, int keep_size, off_t offset, size_t len)
{
	int              ret = -1;
	xlator_t        *subvol = NULL;
	fd_t		*fd = NULL;
        dict_t          *fop_attr = NULL;

        DECLARE_OLD_THIS;
	__GLFS_ENTRY_VALIDATE_FD (glfd, invalid_fs);

        GF_REF_GET (glfd);

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

        ret = get_fop_attr_thrd_key (&fop_attr);
        if (ret)
                gf_msg_debug ("gfapi", 0, "Getting leaseid from thread failed");

	ret = syncop_fallocate (subvol, fd, keep_size, offset, len, fop_attr, NULL);
        DECODE_SYNCOP_ERR (ret);
out:
	if (fd)
		fd_unref(fd);
        if (glfd)
                GF_REF_PUT (glfd);
        if (fop_attr)
                dict_unref (fop_attr);

	glfs_subvol_done (glfd->fs, subvol);

        __GLFS_EXIT_FS;

invalid_fs:
	return ret;
}

GFAPI_SYMVER_PUBLIC_DEFAULT(glfs_fallocate, 3.5.0);


int
pub_glfs_discard (struct glfs_fd *glfd, off_t offset, size_t len)
{
	int              ret = -1;
	xlator_t        *subvol = NULL;
	fd_t		*fd = NULL;
        dict_t          *fop_attr = NULL;

        DECLARE_OLD_THIS;
	__GLFS_ENTRY_VALIDATE_FD (glfd, invalid_fs);

        GF_REF_GET (glfd);

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

        ret = get_fop_attr_thrd_key (&fop_attr);
        if (ret)
                gf_msg_debug ("gfapi", 0, "Getting leaseid from thread failed");

	ret = syncop_discard (subvol, fd, offset, len, fop_attr, NULL);
        DECODE_SYNCOP_ERR (ret);
out:
	if (fd)
		fd_unref(fd);
        if (glfd)
                GF_REF_PUT (glfd);
        if (fop_attr)
                dict_unref (fop_attr);

	glfs_subvol_done (glfd->fs, subvol);

        __GLFS_EXIT_FS;

invalid_fs:
	return ret;
}

GFAPI_SYMVER_PUBLIC_DEFAULT(glfs_discard, 3.5.0);


int
pub_glfs_zerofill (struct glfs_fd *glfd, off_t offset, off_t len)
{
        int               ret             = -1;
        xlator_t         *subvol          = NULL;
        fd_t             *fd              = NULL;
        dict_t           *fop_attr         = NULL;

        DECLARE_OLD_THIS;
        __GLFS_ENTRY_VALIDATE_FD (glfd, invalid_fs);

        GF_REF_GET (glfd);

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

        ret = get_fop_attr_thrd_key (&fop_attr);
        if (ret)
                gf_msg_debug ("gfapi", 0, "Getting leaseid from thread failed");

        ret = syncop_zerofill (subvol, fd, offset, len, fop_attr, NULL);
        DECODE_SYNCOP_ERR (ret);
out:
        if (fd)
                fd_unref(fd);
        if (glfd)
                GF_REF_PUT (glfd);
        if (fop_attr)
                dict_unref (fop_attr);

        glfs_subvol_done (glfd->fs, subvol);

        __GLFS_EXIT_FS;

invalid_fs:
        return ret;
}

GFAPI_SYMVER_PUBLIC_DEFAULT(glfs_zerofill, 3.5.0);


int
pub_glfs_chdir (struct glfs *fs, const char *path)
{
	int              ret = -1;
	xlator_t        *subvol = NULL;
	loc_t            loc = {0, };
	struct iatt      iatt = {0, };
	int              reval = 0;

        DECLARE_OLD_THIS;
        __GLFS_ENTRY_VALIDATE_FS (fs, invalid_fs);

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

        __GLFS_EXIT_FS;

invalid_fs:
	return ret;
}

GFAPI_SYMVER_PUBLIC_DEFAULT(glfs_chdir, 3.4.0);


int
pub_glfs_fchdir (struct glfs_fd *glfd)
{
	int       ret = -1;
	inode_t  *inode = NULL;
	xlator_t *subvol = NULL;
	fd_t     *fd = NULL;

        DECLARE_OLD_THIS;
	__GLFS_ENTRY_VALIDATE_FD (glfd, invalid_fs);

        GF_REF_GET (glfd);

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
        if (glfd)
                GF_REF_PUT (glfd);

	glfs_subvol_done (glfd->fs, subvol);

        __GLFS_EXIT_FS;

invalid_fs:
	return ret;
}

GFAPI_SYMVER_PUBLIC_DEFAULT(glfs_fchdir, 3.4.0);

static gf_boolean_t warn_realpath = _gf_true; /* log once */

static char *
glfs_realpath_common (struct glfs *fs, const char *path, char *resolved_path,
                      gf_boolean_t warn_deprecated)
{
	int              ret = -1;
	char            *retpath = NULL;
	char            *allocpath = NULL;
	xlator_t        *subvol = NULL;
	loc_t            loc = {0, };
	struct iatt      iatt = {0, };
	int              reval = 0;

        DECLARE_OLD_THIS;
        __GLFS_ENTRY_VALIDATE_FS (fs, invalid_fs);

	if (resolved_path)
		retpath = resolved_path;
        else if (warn_deprecated) {
                retpath = allocpath = malloc (PATH_MAX + 1);
                if (warn_realpath) {
                        warn_realpath = _gf_false;
                        gf_log (THIS->name, GF_LOG_WARNING, "this application "
                                "is compiled against an old version of "
                                "libgfapi, it should use glfs_free() to "
                                "release the path returned by "
                                "glfs_realpath()");
                }
        } else {
                retpath = allocpath = GLFS_CALLOC (1, PATH_MAX + 1, NULL,
                                                   glfs_mt_realpath_t);
        }

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
                if (warn_deprecated && allocpath)
                        free (allocpath);
                else if (allocpath)
                        GLFS_FREE (allocpath);
                retpath = NULL;
	}

	glfs_subvol_done (fs, subvol);

        __GLFS_EXIT_FS;

invalid_fs:
	return retpath;
}


char *
pub_glfs_realpath34 (struct glfs *fs, const char *path, char *resolved_path)
{
        return glfs_realpath_common (fs, path, resolved_path, _gf_true);
}

GFAPI_SYMVER_PUBLIC(glfs_realpath34, glfs_realpath, 3.4.0);

char *
pub_glfs_realpath (struct glfs *fs, const char *path, char *resolved_path)
{
        return glfs_realpath_common (fs, path, resolved_path, _gf_false);
}

GFAPI_SYMVER_PUBLIC_DEFAULT(glfs_realpath, 3.7.17);


char *
pub_glfs_getcwd (struct glfs *fs, char *buf, size_t n)
{
	int              ret = -1;
	inode_t         *inode = NULL;
	char            *path = NULL;

        DECLARE_OLD_THIS;
        __GLFS_ENTRY_VALIDATE_FS (fs, invalid_fs);

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

        __GLFS_EXIT_FS;

invalid_fs:
	if (ret < 0)
		return NULL;

	return buf;
}

GFAPI_SYMVER_PUBLIC_DEFAULT(glfs_getcwd, 3.4.0);


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

static int
glfs_lock_common (struct glfs_fd *glfd, int cmd, struct flock *flock,
                  dict_t *xdata)
{
        int              ret = -1;
        xlator_t        *subvol = NULL;
        struct gf_flock  gf_flock = {0, };
        struct gf_flock  saved_flock = {0, };
        fd_t            *fd = NULL;

        DECLARE_OLD_THIS;
        __GLFS_ENTRY_VALIDATE_FD (glfd, invalid_fs);

        if (!flock) {
                errno = EINVAL;
                goto out;
        }

        GF_REF_GET (glfd);
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

        /* Generate glusterfs flock structure from client flock
         * structure to be processed by server */
        gf_flock_from_flock (&gf_flock, flock);

        /* Keep another copy of flock for split/merge of locks
         * at client side */
        gf_flock_from_flock (&saved_flock, flock);

        if (glfd->lk_owner.len != 0) {
                ret = syncopctx_setfslkowner (&glfd->lk_owner);

                if (ret)
                        goto out;
        }

        ret = get_fop_attr_thrd_key (&xdata);
        if (ret)
                gf_msg_debug ("gfapi", 0, "Getting leaseid from thread failed");

        ret = syncop_lk (subvol, fd, cmd, &gf_flock, xdata, NULL);
        DECODE_SYNCOP_ERR (ret);

        /* Convert back from gf_flock to flock as expected by application */
        gf_flock_to_flock (&gf_flock, flock);

        if (ret == 0 && (cmd == F_SETLK || cmd == F_SETLKW)) {
                ret = fd_lk_insert_and_merge (fd, cmd, &saved_flock);
                if (ret) {
                        gf_msg (THIS->name, GF_LOG_ERROR, 0,
                                API_MSG_LOCK_INSERT_MERGE_FAILED,
                                "Lock insertion and splitting/merging failed "
                                "on gfid %s", uuid_utoa (fd->inode->gfid));
                        ret = 0;
                }
        }

out:
        if (fd)
                fd_unref (fd);
        if (glfd)
                GF_REF_PUT (glfd);

        glfs_subvol_done (glfd->fs, subvol);

        __GLFS_EXIT_FS;

invalid_fs:
        return ret;
}

int
pub_glfs_file_lock (struct glfs_fd *glfd, int cmd, struct flock *flock,
                    enum glfs_lock_mode_t lk_mode)
{
        int              ret            = -1;
        dict_t          *xdata_in       = NULL;

        if (lk_mode == GLFS_LK_MANDATORY) {
                /* Create a new dictionary */
                xdata_in = dict_new ();
                if (xdata_in == NULL) {
                        ret = -1;
                        errno = ENOMEM;
                        goto out;
                }

                /* Set GF_LK_MANDATORY internally within dictionary to map
                 * GLFS_LK_MANDATORY */
                ret = dict_set_uint32 (xdata_in, GF_LOCK_MODE, GF_LK_MANDATORY);
                if (ret) {
                        gf_msg (THIS->name, GF_LOG_ERROR, 0,
                                API_MSG_SETTING_LOCK_TYPE_FAILED,
                                "Setting lock type failed");
                        ret = -1;
                        errno = ENOMEM;
                        goto out;
                }
        }

        ret = glfs_lock_common (glfd, cmd, flock, xdata_in);
out:
        if (xdata_in)
                dict_unref (xdata_in);

        return ret;
}

GFAPI_SYMVER_PUBLIC_DEFAULT(glfs_file_lock, 4.0.0);

int
pub_glfs_posix_lock (struct glfs_fd *glfd, int cmd, struct flock *flock)
{
        return glfs_lock_common (glfd, cmd, flock, NULL);
}

GFAPI_SYMVER_PUBLIC_DEFAULT(glfs_posix_lock, 3.4.0);

int
pub_glfs_fd_set_lkowner (glfs_fd_t *glfd, void *data, int len)
{
        int ret = -1;

        DECLARE_OLD_THIS;
        __GLFS_ENTRY_VALIDATE_FD (glfd, invalid_fs);

        if (!GF_REF_GET (glfd)) {
                goto invalid_fs;
        }

        GF_VALIDATE_OR_GOTO (THIS->name, data, out);

        if ((len <= 0) || (len > GFAPI_MAX_LOCK_OWNER_LEN)) {
                errno = EINVAL;
                gf_msg (THIS->name, GF_LOG_ERROR, errno,
                        LG_MSG_INVALID_ARG,
                        "Invalid lk_owner len (%d)", len);
                goto out;
        }

        glfd->lk_owner.len = len;

        memcpy (glfd->lk_owner.data, data, len);

        ret = 0;
out:
        if (glfd)
                GF_REF_PUT (glfd);

        __GLFS_EXIT_FS;

invalid_fs:
        return ret;
}
GFAPI_SYMVER_PUBLIC_DEFAULT(glfs_fd_set_lkowner, 3.10.7);

struct glfs_fd *
pub_glfs_dup (struct glfs_fd *glfd)
{
	xlator_t  *subvol = NULL;
	fd_t      *fd = NULL;
	glfs_fd_t *dupfd = NULL;
	struct glfs *fs = NULL;

        DECLARE_OLD_THIS;
	__GLFS_ENTRY_VALIDATE_FD (glfd, invalid_fs);

        GF_REF_GET (glfd);

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
        dupfd->state = glfd->state;
out:
	if (fd)
		fd_unref (fd);
	if (dupfd)
		glfs_fd_bind (dupfd);
        if (glfd)
                GF_REF_PUT (glfd);

	glfs_subvol_done (fs, subvol);

        __GLFS_EXIT_FS;

invalid_fs:
	return dupfd;
}

GFAPI_SYMVER_PUBLIC_DEFAULT(glfs_dup, 3.4.0);

static void
glfs_enqueue_upcall_data (struct glfs *fs, struct gf_upcall *upcall_data)
{
        int ret = -1;
        upcall_entry *u_list = NULL;

        if (!fs || !upcall_data)
                goto out;

        u_list = GF_CALLOC (1, sizeof(*u_list),
                            glfs_mt_upcall_entry_t);

        if (!u_list) {
                gf_msg (THIS->name, GF_LOG_ERROR, ENOMEM, API_MSG_ALLOC_FAILED,
                        "Upcall entry allocation failed.");
                goto out;
        }

        INIT_LIST_HEAD (&u_list->upcall_list);

        gf_uuid_copy (u_list->upcall_data.gfid, upcall_data->gfid);
        u_list->upcall_data.event_type = upcall_data->event_type;

        switch (upcall_data->event_type) {
        case GF_UPCALL_CACHE_INVALIDATION:
                ret = glfs_get_upcall_cache_invalidation (&u_list->upcall_data,
                                                          upcall_data);
                break;
        case GF_UPCALL_RECALL_LEASE:
                ret = glfs_get_upcall_lease (&u_list->upcall_data,
                                                   upcall_data);
                break;
        default:
                break;
        }

        if (ret) {
                gf_msg (THIS->name, GF_LOG_ERROR, errno,
                        API_MSG_INVALID_ENTRY,
                        "Upcall entry validation failed.");
                goto out;
        }

        pthread_mutex_lock (&fs->upcall_list_mutex);
        {
                list_add_tail (&u_list->upcall_list,
                               &fs->upcall_list);
        }
        pthread_mutex_unlock (&fs->upcall_list_mutex);

        ret = 0;

out:
        if (ret && u_list) {
                GF_FREE (u_list->upcall_data.data);
                GF_FREE(u_list);
        }
}

static void
glfs_free_upcall_lease (void *to_free)
{
        struct glfs_upcall_lease *arg = to_free;

        if (!arg)
                return;

        if (arg->object)
                glfs_h_close (arg->object);

        GF_FREE (arg);
}

int
glfs_recall_lease_fd (struct glfs *fs,
                      struct gf_upcall *up_data)
{
        struct gf_upcall_recall_lease *recall_lease = NULL;
        xlator_t                      *subvol       = NULL;
        int                            ret          = 0;
        inode_t                       *inode        = NULL;
        struct glfs_fd                *glfd         = NULL;
        struct glfs_fd                *tmp          = NULL;
        struct list_head               glfd_list;
        fd_t                          *fd           = NULL;
        uint64_t                       value        = 0;
        struct glfs_lease              lease        = {0, };

        GF_VALIDATE_OR_GOTO ("gfapi", up_data, out);
        GF_VALIDATE_OR_GOTO ("gfapi", fs, out);

        recall_lease = up_data->data;
        GF_VALIDATE_OR_GOTO ("gfapi", recall_lease, out);

        subvol = glfs_active_subvol (fs);
        if (!subvol) {
                ret = -1;
                errno = EIO;
                goto out;
        }

        gf_msg_debug (THIS->name, 0,
                      "Recall lease received for gfid:%s",
                      uuid_utoa(up_data->gfid));


        inode = inode_find (subvol->itable, up_data->gfid);
        if (!inode) {
                ret = -1;
                gf_msg (THIS->name, GF_LOG_ERROR, errno,
                        API_MSG_INODE_FIND_FAILED,
                        "Unable to find inode entry for gfid:%s graph id:%d",
                        uuid_utoa(up_data->gfid), subvol->graph->id);
                goto out;
        }

        LOCK (&inode->lock);
        {
                list_for_each_entry (fd, &inode->fd_list, inode_list) {
                        ret = fd_ctx_get (fd, subvol, &value);
                        glfd = (void *) value;
                        if (glfd) {
                                gf_msg_trace (THIS->name, 0,
                                              "glfd (%p) has held lease", glfd);
                                GF_REF_GET (glfd);
                                list_add_tail (&glfd->list, &glfd_list);
                        }
                }
        }
        UNLOCK (&inode->lock);

        list_for_each_entry_safe (glfd, tmp, &glfd_list, list) {
                LOCK (&glfd->lock);
                {
                        if (glfd->state != GLFD_CLOSE) {
                                gf_msg_trace (THIS->name, 0,
                                              "glfd (%p) has held lease, "
                                              "calling recall cbk", glfd);
                                glfd->cbk (lease, glfd->cookie);
                        }
                }
                UNLOCK (&glfd->lock);

                list_del_init (&glfd->list);
                GF_REF_PUT (glfd);
        }

out:
        return ret;
}

int
glfs_recall_lease_upcall (struct glfs *fs,
                          struct glfs_upcall *up_arg,
                          struct gf_upcall *up_data)
{
        struct gf_upcall_recall_lease *recall_lease = NULL;
        struct glfs_object                  *object       = NULL;
        xlator_t                      *subvol       = NULL;
        int                            ret          = 0;
        struct glfs_upcall_lease      *up_lease_arg = NULL;

        GF_VALIDATE_OR_GOTO ("gfapi", up_data, out);
        GF_VALIDATE_OR_GOTO ("gfapi", fs, out);

        recall_lease = up_data->data;
        GF_VALIDATE_OR_GOTO ("gfapi", recall_lease, out);

        subvol = glfs_active_subvol (fs);
        if (!subvol) {
                ret = -1;
                errno = EIO;
                goto out;
        }

        gf_msg_debug (THIS->name, 0,
                      "Recall lease received for gfid:%s",
                      uuid_utoa(up_data->gfid));

        object = glfs_h_find_handle (fs, up_data->gfid,
                                     GFAPI_HANDLE_LENGTH);
        if (!object) {
                /* The reason handle creation will fail is because we
                 * couldn't find the inode in the gfapi inode table.
                 *
                 * But since application would have taken inode_ref, the
                 * only case when this can happen is when it has closed
                 * the handle and hence will no more be interested in
                 * the upcall for this particular gfid.
                 */
                gf_msg (THIS->name, GF_LOG_DEBUG, errno,
                        API_MSG_CREATE_HANDLE_FAILED,
                        "handle creation of %s failed",
                         uuid_utoa (up_data->gfid));
                errno = ESTALE;
                goto out;
        }

        up_lease_arg = GF_CALLOC (1, sizeof (struct glfs_upcall_lease),
                                  glfs_mt_upcall_inode_t);
        up_lease_arg->object = object;

        GF_VALIDATE_OR_GOTO ("glfs_recall_lease",
                             up_lease_arg, out);


        up_lease_arg->lease_type = recall_lease->lease_type;

        up_arg->reason = GLFS_UPCALL_RECALL_LEASE;
        up_arg->event = up_lease_arg;
        up_arg->free_event = glfs_free_upcall_lease;

        ret = 0;

out:
        if (ret) {
                /* Close p_object and oldp_object as well if being referenced.*/
                if (object)
                        glfs_h_close (object);

                /* Set reason to prevent applications from using ->event */
                up_arg->reason = GF_UPCALL_EVENT_NULL;
        }
        return ret;
}

static int
upcall_syncop_args_free(struct upcall_syncop_args *args)
{
    if (args && args->up_arg)
        GLFS_FREE(args->up_arg);
    GF_FREE(args);
    return 0;
}

static int
glfs_upcall_syncop_cbk(int ret, call_frame_t *frame, void *opaque)
{
    struct upcall_syncop_args *args = opaque;

    /* Here we not using upcall_syncop_args_free as application
     * will be cleaning up the args->up_arg using glfs_free
     * post processing upcall.
     */
    if (ret) {
        upcall_syncop_args_free(args);
    } else
        GF_FREE(args);

    return 0;
}

static int
glfs_cbk_upcall_syncop(void *opaque)
{
        struct upcall_syncop_args *args = opaque;
        struct glfs_upcall *up_arg = NULL;
        struct glfs *fs;

        fs = args->fs;
        up_arg = args->up_arg;

        if (fs->up_cbk && up_arg) {
                (fs->up_cbk)(up_arg, fs->up_data);
                return 0;
        }

        return -1;
}

static struct upcall_syncop_args *
upcall_syncop_args_init(struct glfs *fs, struct gf_upcall *upcall_data)
{
        struct upcall_syncop_args *args = NULL;
        int ret = -1;
        struct glfs_upcall *up_arg = NULL;

        if (!fs || !upcall_data)
                goto out;

        up_arg = GLFS_CALLOC(1, sizeof(struct gf_upcall), glfs_release_upcall,
                             glfs_mt_upcall_entry_t);
        if (!up_arg) {
                gf_msg(THIS->name, GF_LOG_ERROR, ENOMEM, API_MSG_ALLOC_FAILED,
                        "Upcall entry allocation failed.");
                goto out;
        }

        switch (upcall_data->event_type) {
        case GF_UPCALL_CACHE_INVALIDATION:
                ret = glfs_h_poll_cache_invalidation (fs, up_arg, upcall_data);
                break;
        case GF_UPCALL_RECALL_LEASE:
                ret = glfs_recall_lease_upcall (fs, up_arg, upcall_data);
                break;
        default:
                errno = EINVAL;
        }

        /* It could so happen that the file which got
         * upcall notification may have got deleted by
         * the same client. In such cases up_arg->reason
         * is set to GLFS_UPCALL_EVENT_NULL. No need to
         * send upcall then
         */
        if (up_arg->reason == GLFS_UPCALL_EVENT_NULL) {
                gf_msg(THIS->name, GF_LOG_DEBUG, errno, API_MSG_INVALID_ENTRY,
                        "Upcall_EVENT_NULL received. Skipping it.");
                goto out;
        } else if (ret) {
                gf_msg(THIS->name, GF_LOG_ERROR, errno, API_MSG_INVALID_ENTRY,
                        "Upcall entry validation failed.");
                goto out;
        }

        args = GF_CALLOC(1, sizeof(struct upcall_syncop_args),
                        glfs_mt_upcall_entry_t);
        if (!args) {
                gf_msg(THIS->name, GF_LOG_ERROR, ENOMEM, API_MSG_ALLOC_FAILED,
                        "Upcall syncop args allocation failed.");
                goto out;
        }

        /* Note: we are not taking any ref on fs here.
         * Ideally applications have to unregister for upcall events
         * or stop polling for upcall events before performing
         * glfs_fini. And as for outstanding synctasks created, we wait
         * for all syncenv threads to finish tasks before cleaning up the
         * fs->ctx. Hence it seems safe to process these callback
         * notification without taking any lock/ref.
         */
        args->fs = fs;
        args->up_arg = up_arg;

        /* application takes care of calling glfs_free on up_arg post
         * their processing */

    return args;
out:
        if (up_arg) {
                GLFS_FREE(up_arg);
        }

        return NULL;
}

static void
glfs_cbk_upcall_data(struct glfs *fs, struct gf_upcall *upcall_data)
{
    struct upcall_syncop_args *args = NULL;
    int ret = -1;

    if (!fs || !upcall_data)
        goto out;

    if (!(fs->upcall_events & upcall_data->event_type)) {
        /* ignore events which application hasn't registered*/
        goto out;
    }

    args = upcall_syncop_args_init(fs, upcall_data);

    if (!args)
        goto out;

    ret = synctask_new(THIS->ctx->env, glfs_cbk_upcall_syncop,
                       glfs_upcall_syncop_cbk, NULL, args);
    /* should we retry incase of failure? */
    if (ret) {
        gf_msg(THIS->name, GF_LOG_ERROR, errno, API_MSG_UPCALL_SYNCOP_FAILED,
               "Synctak for Upcall event_type(%d) and gfid(%s) failed",
               upcall_data->event_type, (char *)(upcall_data->gfid));
        upcall_syncop_args_free(args);
    }

out:
    return;
}

/*
 * This routine is called in case of any notification received
 * from the server. All the upcall events are queued up in a list
 * to be read by the applications.
 *
 * In case if the application registers a cbk function, that shall
 * be called by this routine incase of any event received.
 * The cbk fn is responsible for notifying the
 * applications the way it desires for each event queued (for eg.,
 * can raise a signal or broadcast a cond variable etc.)
 *
 * Otherwise all the upcall events are queued up in a list
 * to be read/polled by the applications.
 */
void
priv_glfs_process_upcall_event (struct glfs *fs, void *data)
{
        glusterfs_ctx_t    *ctx            = NULL;
        struct gf_upcall   *upcall_data    = NULL;

        DECLARE_OLD_THIS;

        gf_msg_debug (THIS->name, 0,
                      "Upcall gfapi callback is called");

        __GLFS_ENTRY_VALIDATE_FS (fs, err);

        if (!data)
                goto out;

        /* Unlike in I/O path, "glfs_fini" would not have freed
         * 'fs' by the time we take lock as it waits for all epoll
         * threads to exit including this
         */
        pthread_mutex_lock (&fs->mutex);
        {
                ctx = fs->ctx;

                /* if we're not interested in upcalls (anymore), skip them */
                if (ctx->cleanup_started || !fs->cache_upcalls) {
                        pthread_mutex_unlock (&fs->mutex);
                        goto out;
                }

                fs->pin_refcnt++;
        }
        pthread_mutex_unlock (&fs->mutex);

        upcall_data = (struct gf_upcall *)data;

        gf_msg_trace (THIS->name, 0, "Upcall gfapi gfid = %s" ,
                      (char *)(upcall_data->gfid));

        /* *
         * TODO: RECALL LEASE for each glfd
         *
         * In case of RECALL_LEASE, we could associate separate
         * cbk function for each glfd either by
         * - extending pub_glfs_lease to accept new args (recall_cbk_fn, cookie)
         * - or by defining new API "glfs_register_recall_cbk_fn (glfd, recall_cbk_fn, cookie)
         * . In such cases, flag it and instead of calling below upcall functions, define
         * a new one to go through the glfd list and invoke each of theirs recall_cbk_fn.
         * */

        if (fs->up_cbk) { /* upcall cbk registered */
                (void) glfs_cbk_upcall_data (fs, upcall_data);
        } else {
                (void) glfs_enqueue_upcall_data (fs, upcall_data);
        }

        pthread_mutex_lock (&fs->mutex);
        {
                fs->pin_refcnt--;
        }
        pthread_mutex_unlock (&fs->mutex);

out:
        __GLFS_EXIT_FS;
err:
        return;
}
GFAPI_SYMVER_PRIVATE_DEFAULT(glfs_process_upcall_event, 3.7.0);

ssize_t
glfs_anonymous_pwritev (struct glfs *fs, struct glfs_object *object,
                        const struct iovec *iovec, int iovcnt,
                        off_t offset, int flags)
{
        xlator_t        *subvol = NULL;
        struct iobref   *iobref = NULL;
        struct iobuf    *iobuf  = NULL;
        struct iovec    iov     = {0, };
        inode_t         *inode  = NULL;
        fd_t            *fd     = NULL;
        int             ret     = -1;
        size_t          size    = -1;

        DECLARE_OLD_THIS;
        __GLFS_ENTRY_VALIDATE_FS (fs, invalid_fs);

        subvol = glfs_active_subvol (fs);
        if (!subvol) {
                ret = -1;
                errno = EIO;
                goto out;
        }

        /* get/refresh the in arg objects inode in correlation to the xlator */
        inode = glfs_resolve_inode (fs, subvol, object);
        if (!inode) {
                ret = -1;
                errno = ESTALE;
                goto out;
        }

        fd = fd_anonymous (inode);
        if (!fd) {
                ret = -1;
                gf_msg ("gfapi", GF_LOG_ERROR, ENOMEM, API_MSG_FDCREATE_FAILED,
                        "Allocating anonymous fd failed");
                errno = ENOMEM;
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

        iov_unload (iobuf_ptr (iobuf), iovec, iovcnt);

        iov.iov_base = iobuf_ptr (iobuf);
        iov.iov_len = size;

        /* TODO : set leaseid */
        ret = syncop_writev (subvol, fd, &iov, 1, offset, iobref, flags,
                             NULL, NULL);
        DECODE_SYNCOP_ERR (ret);

        iobuf_unref (iobuf);
        iobref_unref (iobref);

        if (ret <= 0)
                goto out;

out:

        if (fd)
                fd_unref(fd);

        if (inode)
                inode_unref (inode);

        glfs_subvol_done (fs, subvol);

        __GLFS_EXIT_FS;

invalid_fs:
        return ret;
}

ssize_t
glfs_anonymous_preadv (struct glfs *fs,  struct glfs_object *object,
                       const struct iovec *iovec, int iovcnt,
                       off_t offset, int flags)
{
        xlator_t        *subvol = NULL;
        struct iovec    *iov    = NULL;
        struct iobref   *iobref = NULL;
        inode_t         *inode  = NULL;
        fd_t            *fd     = NULL;
        int             cnt     = 0;
        ssize_t         ret     = -1;
        ssize_t         size    = -1;

        DECLARE_OLD_THIS;
        __GLFS_ENTRY_VALIDATE_FS (fs, invalid_fs);

        subvol = glfs_active_subvol (fs);
        if (!subvol) {
                ret = -1;
                errno = EIO;
                goto out;
        }

        /* get/refresh the in arg objects inode in correlation to the xlator */
        inode = glfs_resolve_inode (fs, subvol, object);
        if (!inode) {
                ret = -1;
                errno = ESTALE;
                goto out;
        }

        fd = fd_anonymous (inode);
        if (!fd) {
                ret = -1;
                gf_msg ("gfapi", GF_LOG_ERROR, ENOMEM, API_MSG_FDCREATE_FAILED,
                        "Allocating anonymous fd failed");
                errno = ENOMEM;
                goto out;
        }

        size = iov_length (iovec, iovcnt);

        /* TODO : set leaseid */
	ret = syncop_readv (subvol, fd, size, offset, flags, &iov, &cnt,
                            &iobref, NULL, NULL);
        DECODE_SYNCOP_ERR (ret);
        if (ret <= 0)
                goto out;

        size = iov_copy (iovec, iovcnt, iov, cnt);

        ret = size;
out:
        if (iov)
                GF_FREE (iov);
        if (iobref)
                iobref_unref (iobref);
        if (fd)
                fd_unref(fd);

        if (inode)
                inode_unref (inode);

        glfs_subvol_done (fs, subvol);

        __GLFS_EXIT_FS;

invalid_fs:
        return ret;
}

static void glfs_release_xreaddirp_stat (void *ptr)
{
        struct glfs_xreaddirp_stat *to_free = ptr;

        if (to_free->object)
                glfs_h_close (to_free->object);
}

/*
 * Given glfd of a directory, this function does readdirp and returns
 * xstat along with dirents.
 */
int
pub_glfs_xreaddirplus_r (struct glfs_fd *glfd, uint32_t flags,
                         struct glfs_xreaddirp_stat **xstat_p,
                         struct dirent *ext,
                         struct dirent **res)
{
        int              ret = -1;
        gf_dirent_t     *entry = NULL;
        struct dirent   *buf = NULL;
        struct glfs_xreaddirp_stat *xstat = NULL;

        DECLARE_OLD_THIS;
        __GLFS_ENTRY_VALIDATE_FD (glfd, invalid_fs);

        GF_REF_GET (glfd);

        GF_VALIDATE_OR_GOTO (THIS->name, xstat_p, out);
        GF_VALIDATE_OR_GOTO (THIS->name, res, out);

        errno = 0;

        if (ext)
                buf = ext;
        else
                buf = glfs_readdirbuf_get (glfd);

        if (!buf)
                goto out;

        xstat = GLFS_CALLOC(1, sizeof(struct glfs_xreaddirp_stat),
                            glfs_release_xreaddirp_stat,
                            glfs_mt_xreaddirp_stat_t);

        if (!xstat)
                goto out;

        /* this is readdirplus operation */
        entry = glfd_entry_next (glfd, 1);

        /* XXX: Ideally when we reach EOD, errno should have been
         * set to ENOENT. But that doesn't seem to be the case.
         *
         * The only way to confirm if its EOD at this point is that
         * errno == 0 and entry == NULL
         */
        if (errno)
                goto out;

        if (!entry) {
                /* reached EOD, ret = 0  */
                ret = 0;
                *res = NULL;
                *xstat_p = NULL;

                /* free xstat as applications shall not be using it */
                GLFS_FREE (xstat);

                goto out;
        }

        *res = buf;
        gf_dirent_to_dirent (entry, buf);

        if (flags & GFAPI_XREADDIRP_STAT) {
                glfs_iatt_to_stat (glfd->fs, &entry->d_stat, &xstat->st);
                xstat->flags_handled |= GFAPI_XREADDIRP_STAT;
        }

        if ((flags & GFAPI_XREADDIRP_HANDLE) &&
                /* skip . and .. */
                strcmp(buf->d_name, ".")
                     && strcmp(buf->d_name, "..")) {

                /* Now create object.
                 * We can use "glfs_h_find_handle" as well as inodes would have
                 * already got linked as part of 'gf_link_inodes_from_dirent' */
                xstat->object = glfs_h_create_from_handle (glfd->fs,
                                                        entry->d_stat.ia_gfid,
                                                        GFAPI_HANDLE_LENGTH,
                                                        NULL);

                if (xstat->object) { /* success */
                        /* note: xstat->object->inode->ref is taken
                         * This shall be unref'ed when application does
                         * glfs_free(xstat) */
                        xstat->flags_handled |= GFAPI_XREADDIRP_HANDLE;
                }
        }

        ret = xstat->flags_handled;
        *xstat_p = xstat;

        gf_msg_debug (THIS->name, 0,
                      "xreaddirp- requested_flags (%x) , processed_flags (%x)",
                      flags, xstat->flags_handled);

out:
        GF_REF_PUT (glfd);

        if (ret < 0) {
                gf_msg (THIS->name, GF_LOG_WARNING, errno,
                        API_MSG_XREADDIRP_R_FAILED,
                        "glfs_x_readdirp_r failed - reason (%s)",
                        strerror(errno));

                if (xstat)
                        GLFS_FREE (xstat);
        }

        __GLFS_EXIT_FS;

        return ret;

invalid_fs:
        return -1;
}
GFAPI_SYMVER_PUBLIC_DEFAULT(glfs_xreaddirplus_r, 3.11.0);

struct stat*
pub_glfs_xreaddirplus_get_stat (struct glfs_xreaddirp_stat *xstat)
{
        GF_VALIDATE_OR_GOTO ("glfs_xreaddirplus_get_stat", xstat, out);

        if (!xstat->flags_handled & GFAPI_XREADDIRP_STAT)
                gf_msg (THIS->name, GF_LOG_ERROR, errno,
                        LG_MSG_INVALID_ARG,
                        "GFAPI_XREADDIRP_STAT is not set. Flags"
                        "handled for xstat(%p) are (%x)",
                        xstat, xstat->flags_handled);
        return &xstat->st;

out:
        return NULL;
}
GFAPI_SYMVER_PUBLIC_DEFAULT(glfs_xreaddirplus_get_stat, 3.11.0);

void
gf_lease_to_glfs_lease (struct gf_lease *gf_lease, struct glfs_lease *lease)
{
        lease->cmd = gf_lease->cmd;
        lease->lease_type = gf_lease->lease_type;
        memcpy (lease->lease_id, gf_lease->lease_id, LEASE_ID_SIZE);
}

void
glfs_lease_to_gf_lease (struct glfs_lease *lease, struct gf_lease *gf_lease)
{
        gf_lease->cmd = lease->cmd;
        gf_lease->lease_type = lease->lease_type;
        memcpy (gf_lease->lease_id, lease->lease_id, LEASE_ID_SIZE);
}

int
pub_glfs_lease (struct glfs_fd *glfd, struct glfs_lease *lease,
                glfs_recall_cbk fn, void *data)
{
        int              ret = -1;
        loc_t            loc = {0, };
        xlator_t        *subvol = NULL;
        fd_t            *fd = NULL;
        struct gf_lease  gf_lease = {0, };

        DECLARE_OLD_THIS;
        __GLFS_ENTRY_VALIDATE_FD (glfd, invalid_fs);

        GF_REF_GET (glfd);

        if (!is_valid_lease_id (lease->lease_id)) {
                ret = -1;
                errno = EINVAL;
                goto out;
        }

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

        switch (lease->lease_type) {
        case GLFS_RD_LEASE:
                if ((fd->flags != O_RDONLY) && !(fd->flags & O_RDWR)) {
                        ret = -1;
                        errno = EINVAL;
                        goto out;
                }
                break;
        case GLFS_RW_LEASE:
                if (!((fd->flags & O_WRONLY) || (fd->flags & O_RDWR))) {
                        ret = -1;
                        errno = EINVAL;
                        goto out;
                }
                break;
        default:
                if (lease->cmd != GLFS_GET_LEASE) {
                        ret = -1;
                        errno = EINVAL;
                        goto out;
                }
                break;
        }

        /* populate loc */
        GLFS_LOC_FILL_INODE (fd->inode, loc, out);

        glfs_lease_to_gf_lease (lease, &gf_lease);

        ret = syncop_lease (subvol, &loc, &gf_lease, NULL, NULL);
        DECODE_SYNCOP_ERR (ret);

        gf_lease_to_glfs_lease (&gf_lease, lease);

        /* TODO: Add leases for client replay
        if (ret == 0 && (cmd == F_SETLK || cmd == F_SETLKW))
                fd_lk_insert_and_merge (fd, cmd, &saved_flock);
        */
        if (ret == 0) {
                   ret = fd_ctx_set (glfd->fd, subvol, (uint64_t)(long)glfd);
                   if (ret) {
                           gf_msg (subvol->name, GF_LOG_ERROR, ENOMEM, API_MSG_FDCTX_SET_FAILED,
                                           "Setting fd ctx failed for fd(%p)", glfd->fd);
                           goto out;
                   }
                   glfd->cbk = fn;
                   glfd->cookie = data;
        }

out:

        if (glfd)
                GF_REF_PUT (glfd);

        if (subvol)
                glfs_subvol_done (glfd->fs, subvol);

        __GLFS_EXIT_FS;

invalid_fs:
        return ret;
}

GFAPI_SYMVER_PUBLIC_DEFAULT(glfs_lease, 4.0.0);
