/*
  Copyright (c) 2006, 2007, 2008 Z RESEARCH, Inc. <http://www.zresearch.com>
  This file is part of GlusterFS.

  GlusterFS is free software; you can redistribute it and/or modify
  it under the terms of the GNU General Public License as published
  by the Free Software Foundation; either version 3 of the License,
  or (at your option) any later version.

  GlusterFS is distributed in the hope that it will be useful, but
  WITHOUT ANY WARRANTY; without even the implied warranty of
  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
  General Public License for more details.

  You should have received a copy of the GNU General Public License
  along with this program.  If not, see
  <http://www.gnu.org/licenses/>.
*/

#ifndef _CONFIG_H
#define _CONFIG_H
#include "config.h"
#endif

#define __XOPEN_SOURCE 500

#include <stdint.h>
#include <sys/time.h>
#include <errno.h>
#include <ftw.h>
#ifndef GF_BSD_HOST_OS
#include <alloca.h>
#endif
#include <sys/resource.h>

#include "glusterfs.h"
#include "dict.h"
#include "logging.h"
#include "posix.h"
#include "xlator.h"
#include "lock.h"
#include "defaults.h"
#include "common-utils.h"
#include "compat-errno.h"
#include "compat.h"
#include "byte-order.h"

#undef HAVE_SET_FSID
#ifdef HAVE_SET_FSID

#define DECLARE_OLD_FS_ID_VAR uid_t old_fsuid; gid_t old_fsgid;

#define SET_FS_ID(uid, gid) do {		\
                old_fsuid = setfsuid (uid);     \
                old_fsgid = setfsgid (gid);     \
        } while (0)

#define SET_TO_OLD_FS_ID() do {			\
                setfsuid (old_fsuid);           \
                setfsgid (old_fsgid);           \
        } while (0)

#else

#define DECLARE_OLD_FS_ID_VAR
#define SET_FS_ID(uid, gid)
#define SET_TO_OLD_FS_ID()

#endif

#define MAKE_REAL_PATH(var, this, path) do {                            \
		var = alloca (strlen (path) + POSIX_BASE_PATH_LEN(this) + 2); \
                strcpy (var, POSIX_BASE_PATH(this));			\
                strcpy (&var[POSIX_BASE_PATH_LEN(this)], path);		\
        } while (0)

/* Log once in GF_UNIVERSAL_ANSWER times */
#define GF_LOG_OCCASIONALLY(var, args...) if (!(var++ % GF_UNIVERSAL_ANSWER)) {\
                gf_log (args);                                          \
        }


dict_t *
posix_lookup_xattr_fill (xlator_t *this, const char *real_path,
			 int need_xattr, struct stat *buf)
{
        ssize_t     xattr_size         = 0;
        char *      databuf            = NULL;
        const int   val_size           = 64;
        char        version[val_size];
        char        ctime[val_size];
	char        layout[val_size];
	char        linkto[val_size];
	char       *layout_p           = NULL;
	char       *linkto_p           = NULL;
	char       *entry_pending      = NULL;
        int         _fd                = -1;
	dict_t     *xattr              = NULL;
	int         ret                = -1;


	xattr = get_new_dict();
	if (!xattr) {
		gf_log (this->name, GF_LOG_ERROR,
			"memory allocation failed :(");
		goto err;
	}

	xattr_size = lgetxattr (real_path, GLUSTERFS_VERSION, version, 
				val_size);

	/* should size be put into the data_t ? */
	if (xattr_size != -1) {
		version[xattr_size] = '\0';
		dict_set (xattr, GLUSTERFS_VERSION, 
			  data_from_uint32 (strtoll (version, NULL, 10)));
	}

	xattr_size = lgetxattr (real_path, GLUSTERFS_CREATETIME, 
				ctime, val_size);
	if (xattr_size != -1) {
		ctime[xattr_size] = '\0';
		dict_set (xattr, GLUSTERFS_CREATETIME, 
			  data_from_uint32 (strtoll (ctime, NULL, 10)));
	}

	xattr_size = lgetxattr (real_path, "trusted.glusterfs.dht", layout,
				val_size);

	/* should size be put into the data_t ? */
	if (xattr_size != -1) {
		layout[xattr_size] = '\0';
		layout_p = memdup (layout, xattr_size);
		dict_set_bin (xattr, "trusted.glusterfs.dht",
			      layout_p, xattr_size);
	}

	xattr_size = lgetxattr (real_path, "trusted.glusterfs.dht.linkto",
				linkto, val_size);

	/* should size be put into the data_t ? */
	if (xattr_size != -1) {
		linkto[xattr_size] = '\0';
		linkto_p = strdup (linkto);
		dict_set_bin (xattr, "trusted.glusterfs.dht.linkto",
			      linkto_p, xattr_size);
	}

//	xattr_size = lgetxattr (real_path, "trusted.glusterfs.afr.metadata-pending");
//	xattr_size = lgetxattr (real_path, "trusted.glusterfs.afr.data-pending");	

	xattr_size = lgetxattr (real_path, "trusted.glusterfs.afr.entry-pending",
				entry_pending, 0);
	if (xattr_size != -1) {
		entry_pending = malloc (xattr_size);
		lgetxattr (real_path, "trusted.glusterfs.afr.entry-pending",
			   entry_pending, xattr_size);
		dict_set_bin (xattr, "trusted.glusterfs.afr.entry-pending",
			      entry_pending, xattr_size);
	}

	if ((need_xattr > 0)
	    && (buf->st_size <= need_xattr)
	    && S_ISREG (buf->st_mode)) {
		
		_fd = open (real_path, O_RDONLY);

		if (_fd == -1) {
			gf_log (this->name, GF_LOG_ERROR, 
				"opening file %s failed: %s",
				real_path, strerror (errno));
			goto err;
		}
      
		databuf = malloc (buf->st_size);

		if (!databuf) {
			gf_log (this->name, GF_LOG_ERROR, 
				"out of memory :(");
			goto err;
		}
      
		ret = gf_full_read (_fd, databuf, buf->st_size);
		if (ret == -1) {
			gf_log (this->name, GF_LOG_ERROR, 
				"read on file %s failed: %s", 
				real_path, strerror (errno));
			goto err;
		}

		ret = close (_fd);
		_fd = -1;
		if (ret == -1) {
			gf_log (this->name, GF_LOG_ERROR, 
				"close on file %s failed: %s", 
				real_path, strerror (errno));
			goto err;
		}
      
		ret = dict_set_bin (xattr, "glusterfs.content", 
				    databuf, buf->st_size);
		if (ret < 0) {
			goto err;
		}

		/* To avoid double free in cleanup below */
		databuf = NULL;
	}

	return xattr;

err:
	if (_fd != -1)
		close (_fd);
	if (databuf)
		FREE (databuf);
	if (xattr)
		dict_destroy (xattr);

	return NULL;
}


int32_t
posix_lookup (call_frame_t *frame, xlator_t *this,
              loc_t *loc, int32_t need_xattr)
{
        struct stat buf                = {0, };
        char *      real_path          = NULL;
        int32_t     op_ret             = -1;
        int32_t     op_errno           = 0;
        dict_t *    xattr              = NULL;


        VALIDATE_OR_GOTO (frame, out);
        VALIDATE_OR_GOTO (this, out);
        VALIDATE_OR_GOTO (loc, out);
	VALIDATE_OR_GOTO (loc->path, out);

        MAKE_REAL_PATH (real_path, this, loc->path);

        op_ret   = lstat (real_path, &buf);
        op_errno = errno;

        if (op_ret == -1) {
		if (op_errno != ENOENT) {
			gf_log (this->name, GF_LOG_WARNING, "lstat on %s failed: %s", 
				loc->path, strerror (op_errno));
		}
                goto out;
        }

        if (need_xattr && (op_ret == 0)) {
		xattr = posix_lookup_xattr_fill (this, real_path,
						 need_xattr, &buf);

        }
	
	op_ret = 0;
out:
        frame->root->rsp_refs = NULL;

        if (xattr)
                dict_ref (xattr);

        STACK_UNWIND (frame, op_ret, op_errno, loc->inode, &buf, xattr);

        if (xattr)
                dict_unref (xattr);

        return 0;
}


int32_t
posix_forget (call_frame_t *frame,
              xlator_t *this,
              inode_t *inode)
{
        int32_t _fd      = -1;
        int     ret      = -1;
        int32_t op_ret   = -1;
        int32_t op_errno = 0;

        VALIDATE_OR_GOTO (frame, out);
        VALIDATE_OR_GOTO (this, out);
        VALIDATE_OR_GOTO (inode, out);

        ret = dict_get_int32 (inode->ctx, this->name, &_fd);

        if (ret < 0) {
                op_errno = -ret;
                goto out;
        }

        op_ret = close (_fd);

        if (op_ret == -1) {
                op_errno = errno;
                gf_log (this->name, GF_LOG_ERROR, "close failed (%s)", 
                        strerror (errno));
                goto out;
        }

        op_ret = 0;
  
 out:
        return op_ret;
}

int32_t
posix_stat (call_frame_t *frame,
            xlator_t *this,
            loc_t *loc)
{
        struct stat buf       = {0,};
        char *      real_path = NULL;
        int32_t     op_ret    = -1;
        int32_t     op_errno  = 0;

        DECLARE_OLD_FS_ID_VAR;

        VALIDATE_OR_GOTO (frame, out);
        VALIDATE_OR_GOTO (this, out);
        VALIDATE_OR_GOTO (loc, out);

        SET_FS_ID (frame->root->uid, frame->root->gid);
        MAKE_REAL_PATH (real_path, this, loc->path);

        op_ret = lstat (real_path, &buf);

        if (op_ret == -1) {
                op_errno = errno;
                gf_log (this->name, GF_LOG_WARNING, 
                        "lstat on %s: %s", loc->path, strerror (op_errno));
                goto out;
        }

        op_ret = 0;

 out:
        SET_TO_OLD_FS_ID();  
        frame->root->rsp_refs = NULL;
        STACK_UNWIND (frame, op_ret, op_errno, &buf);

        return 0;
}

int32_t 
posix_opendir (call_frame_t *frame, xlator_t *this,
               loc_t *loc, fd_t *fd)
{
        char *            real_path = NULL;
        int32_t           op_ret    = -1;
        int32_t           op_errno  = 0;
        DIR *             dir       = NULL;
        struct posix_fd * pfd       = NULL;

        DECLARE_OLD_FS_ID_VAR;
  
        VALIDATE_OR_GOTO (frame, out);
        VALIDATE_OR_GOTO (this, out);
        VALIDATE_OR_GOTO (loc, out);
        VALIDATE_OR_GOTO (loc->path, out);
        VALIDATE_OR_GOTO (fd, out);

        SET_FS_ID (frame->root->uid, frame->root->gid);
        MAKE_REAL_PATH (real_path, this, loc->path);

        dir = opendir (real_path);

        if (dir == NULL) {
                op_errno = errno;
                gf_log (this->name, GF_LOG_ERROR, 
                        "opendir failed on %s (%s)", loc->path, strerror (op_errno));
                goto out;
        }

        op_ret = dirfd (dir);
  
        pfd = calloc (1, sizeof (*fd));
        if (!pfd) {
                op_errno = errno;
                gf_log (this->name, GF_LOG_ERROR,
                        "out of memory :(");
                goto out;
        }

        pfd->dir = dir;
        pfd->fd = dirfd (dir);
        pfd->path = strdup (real_path);
        if (!pfd->path) {
                gf_log (this->name, GF_LOG_ERROR,
                        "out of memory :(");
                goto out;
        }

        dict_set (fd->ctx, this->name, data_from_static_ptr (pfd));

        frame->root->rsp_refs = NULL;

        op_ret = 0;

 out:
        if (op_ret == -1) {
                if (dir) {
                        closedir (dir);
                        dir = NULL;
                }
                if (pfd) {
                        if (pfd->path)
                                FREE (pfd->path);
                        FREE (pfd);
                        pfd = NULL;
                }
        }

        SET_TO_OLD_FS_ID ();
        STACK_UNWIND (frame, op_ret, op_errno, fd);
        return 0;
}


int32_t
posix_getdents (call_frame_t *frame, xlator_t *this,
                fd_t *fd, size_t size, off_t off, int32_t flag)
{
        int32_t           op_ret         = -1;
        int32_t           op_errno       = 0;
        char *            real_path      = NULL;
        dir_entry_t       entries        = {0, };
        dir_entry_t *     tmp            = NULL;
        DIR *             dir            = NULL;
        struct dirent *   dirent         = NULL;
        int               real_path_len  = -1;
        int               entry_path_len = -1;
        char *            entry_path     = NULL;
        int               count          = 0;
        struct posix_fd * pfd            = NULL;

        struct stat       buf            = {0,};
        int               ret            = -1;
        char              tmp_real_path[GF_PATH_MAX];
        char              linkpath[GF_PATH_MAX];

        DECLARE_OLD_FS_ID_VAR ;
  
        VALIDATE_OR_GOTO (frame, out);
        VALIDATE_OR_GOTO (this, out);
        VALIDATE_OR_GOTO (fd, out);

        SET_FS_ID (frame->root->uid, frame->root->gid);

        if (!fd->ctx) {
                op_errno = EBADFD;
                gf_log (this->name, GF_LOG_ERROR, "fd->ctx is NULL (fd=%p)", fd);
                goto out;
        }

        ret = dict_get_ptr (fd->ctx, this->name, (void **)&pfd);
        if (ret < 0) {
                op_errno = -ret;
                gf_log (this->name, GF_LOG_ERROR, 
                        "fd %p does not have context in %s",
                        fd, this->name);
                goto out;
        }

        if (!pfd->path) {
                op_errno = EBADFD;
                gf_log (this->name, GF_LOG_ERROR,
                        "pfd does not have path set (possibly file fd, fd=%p)", fd);
                goto out;
        }

        real_path     = pfd->path;
        real_path_len = strlen (real_path);

        entry_path_len = real_path_len + NAME_MAX;
        entry_path     = calloc (1, entry_path_len);

        if (!entry_path) {
                op_errno = errno;
                gf_log (this->name, GF_LOG_ERROR,
                        "out of memory :(");
                goto out;
        }

        strncpy (entry_path, real_path, entry_path_len);
        entry_path[real_path_len] = '/';

        dir = pfd->dir;
  
        if (!dir) {
                op_errno = EBADFD;
                gf_log (this->name, GF_LOG_ERROR, 
                        "pfd does not have dir set (possibly file fd, fd=%p, path=`%s'",
                        fd, real_path);
                goto out;
        }

        /* TODO: check for all the type of flag, and behave appropriately */

        while ((dirent = readdir (dir))) {
                if (!dirent)
                        break;

                /* This helps in self-heal, when only directories 
                   needs to be replicated */
    
                /* This is to reduce the network traffic, in case only 
                   directory is needed from posix */

                strncpy (tmp_real_path, real_path, GF_PATH_MAX);
                strncat (tmp_real_path, "/", GF_PATH_MAX - strlen (tmp_real_path));

                strncat (tmp_real_path, dirent->d_name, 
                         GF_PATH_MAX - strlen (tmp_real_path));
                ret = lstat (tmp_real_path, &buf);

                if ((flag == GF_GET_DIR_ONLY) 
                    && (ret != -1 && !S_ISDIR(buf.st_mode))) {
                        continue;
                }

                tmp = calloc (1, sizeof (*tmp));

                if (!tmp) {
                        op_errno = errno;
                        gf_log (this->name, GF_LOG_ERROR,
                                "out of memory :(");
                        goto out;
                }

                tmp->name = strdup (dirent->d_name);
                if (!tmp->name) {
                        op_errno = errno;
                        gf_log (this->name, GF_LOG_ERROR,
                                "out of memory :(");
                        goto out;
                }

                if (entry_path_len < real_path_len + 1 + strlen (tmp->name) + 1) {
                        entry_path_len = real_path_len + strlen (tmp->name) + 1024;

                        entry_path = realloc (entry_path, entry_path_len);
                }

                strcpy (&entry_path[real_path_len+1], tmp->name);

                ret = lstat (entry_path, &tmp->buf);

                if (ret == -1) {
                        op_errno = errno;
                        gf_log (this->name, GF_LOG_ERROR, "lstat on %s failed: %s", 
                                entry_path, strerror (op_errno));
                        goto out;
                }

                if (S_ISLNK(tmp->buf.st_mode)) {

                        ret = readlink (entry_path, linkpath, GF_PATH_MAX);
                        if (ret != -1) {
                                linkpath[ret] = '\0';
                                tmp->link = strdup (linkpath);
                        }
                } else {
                        tmp->link = "";
                }

                count++;

                tmp->next = entries.next;
                entries.next = tmp;

                /* if size is 0, count can never be = size, so entire dir is read */
                if (count == size)
                        break;
        }

        FREE (entry_path);

        op_ret = 0;

 out:
        SET_TO_OLD_FS_ID ();

        if (op_ret == -1) {
                if (entry_path)
                        FREE (entry_path);
        }

        frame->root->rsp_refs = NULL;
        STACK_UNWIND (frame, op_ret, op_errno, &entries, count);

        if (op_ret == 0) {
                while (entries.next) {
                        tmp = entries.next;
                        entries.next = entries.next->next;
                        FREE (tmp->name);
                        FREE (tmp);
                }
        }

        return 0;
}


/*int32_t 
posix_closedir (call_frame_t *frame, xlator_t *this,
                fd_t *fd)
{
        int32_t           op_ret   = -1;
        int32_t           op_errno = 0;
        struct posix_fd * pfd      = NULL;
        int               ret      = 0;

        VALIDATE_OR_GOTO (frame, out);
        VALIDATE_OR_GOTO (this, out);
        VALIDATE_OR_GOTO (fd, out);
        VALIDATE_OR_GOTO (fd->ctx, out);

        ret = dict_get_ptr (fd->ctx, this->name, (void **)&pfd);

        if (ret < 0) {
                op_errno = -ret;
                gf_log (this->name, GF_LOG_ERROR, 
                        "pfd from fd=%p is NULL", fd);
                goto out;
        }

        if (!pfd->dir) {
                op_errno = EINVAL;
                gf_log (this->name, GF_LOG_ERROR, 
                        "pfd->dir is NULL for fd=%p path=%s",
                        fd, pfd->path ? pfd->path : "<NULL>");
                goto out;
        }

        ret = closedir (pfd->dir);
        if (ret == -1) {
                op_errno = errno;
                gf_log (this->name, GF_LOG_ERROR, 
                        "closedir on %p failed", pfd->dir);
                goto out;
        }
        pfd->dir = NULL;
  
        if (!pfd->path) {
                op_errno = EBADFD;
                gf_log (this->name, GF_LOG_ERROR, 
                        "pfd->path was NULL. fd=%p pfd=%p",
                        fd, pfd);
                goto out;
        }

        op_ret = 0;

 out:
        frame->root->rsp_refs = NULL;

        if (pfd) {
                if (pfd->path)
                        FREE (pfd->path);
                dict_del (fd->ctx, this->name);
                FREE (pfd);
        }

        STACK_UNWIND (frame, op_ret, op_errno);

        return 0;
}
*/
int32_t 
posix_closedir (xlator_t *this,
                fd_t *fd)
{
        int32_t           op_ret   = -1;
        int32_t           op_errno = 0;
        struct posix_fd * pfd      = NULL;
        int               ret      = 0;

        VALIDATE_OR_GOTO (this, out);
        VALIDATE_OR_GOTO (fd, out);
        VALIDATE_OR_GOTO (fd->ctx, out);

        ret = dict_get_ptr (fd->ctx, this->name, (void **)&pfd);

        if (ret < 0) {
                op_errno = -ret;
                gf_log (this->name, GF_LOG_ERROR, 
                        "pfd from fd=%p is NULL", fd);
                goto out;
        }

        if (!pfd->dir) {
                op_errno = EINVAL;
                gf_log (this->name, GF_LOG_ERROR, 
                        "pfd->dir is NULL for fd=%p path=%s",
                        fd, pfd->path ? pfd->path : "<NULL>");
                goto out;
        }

        ret = closedir (pfd->dir);
        if (ret == -1) {
                op_errno = errno;
                gf_log (this->name, GF_LOG_ERROR, 
                        "closedir on %p failed", pfd->dir);
                goto out;
        }
        pfd->dir = NULL;
  
        if (!pfd->path) {
                op_errno = EBADFD;
                gf_log (this->name, GF_LOG_ERROR, 
                        "pfd->path was NULL. fd=%p pfd=%p",
                        fd, pfd);
                goto out;
        }

        op_ret = 0;

 out:
        if (pfd) {
                if (pfd->path)
                        FREE (pfd->path);
		FREE (pfd);
        }

        return 0;
}


int32_t 
posix_readlink (call_frame_t *frame, xlator_t *this,
                loc_t *loc, size_t size)
{
        char *  dest      = NULL;
        int32_t op_ret    = -1;
        int32_t op_errno  = 0;
        char *  real_path = NULL;

        DECLARE_OLD_FS_ID_VAR;

	VALIDATE_OR_GOTO (frame, out);

        SET_FS_ID (frame->root->uid, frame->root->gid);

        dest = alloca (size + 1);

        MAKE_REAL_PATH (real_path, this, loc->path);

        op_ret = readlink (real_path, dest, size);
        if (op_ret == -1) {
                op_errno = errno;
                gf_log (this->name, GF_LOG_WARNING, 
                        "readlink on %s: %s", loc->path, strerror (op_errno));
                goto out;
        }

        dest[op_ret] = 0;

 out:
        SET_TO_OLD_FS_ID ();
        frame->root->rsp_refs = NULL;
  
        STACK_UNWIND (frame, op_ret, op_errno, dest);

        return 0;
}

int32_t 
posix_mknod (call_frame_t *frame, xlator_t *this,
             loc_t *loc, mode_t mode, dev_t dev)
{
        int32_t     op_ret    = -1;
        int32_t     op_errno  = 0;
        char *      real_path = 0;
        struct stat stbuf     = { 0, };

        DECLARE_OLD_FS_ID_VAR;
  
        VALIDATE_OR_GOTO (frame, out);
        VALIDATE_OR_GOTO (this, out);
        VALIDATE_OR_GOTO (loc, out);

        SET_FS_ID (frame->root->uid, frame->root->gid);
        MAKE_REAL_PATH (real_path, this, loc->path);

        op_ret = mknod (real_path, mode, dev);

        if (op_ret == -1) {
                op_errno = errno;
                gf_log (this->name, GF_LOG_ERROR,
                        "mknod on %s: %s", loc->path, strerror (op_errno));
                goto out;
        }
  
#ifndef HAVE_SET_FSID
        op_ret = lchown (real_path, frame->root->uid, frame->root->gid);
        if (op_ret == -1) {
                op_errno = errno;
                gf_log (this->name, GF_LOG_ERROR,
                        "lchown on %s: %s", loc->path, strerror (op_errno));
                goto out;
        }
#endif

        op_ret = lstat (real_path, &stbuf);

        if (op_ret == -1) {
                op_errno = errno;
                gf_log (this->name, GF_LOG_ERROR,
                        "mknod on %s: %s", loc->path, strerror (op_errno));
                goto out;
        }

        op_ret = 0;

 out:  
        SET_TO_OLD_FS_ID ();

        frame->root->rsp_refs = NULL;  
        STACK_UNWIND (frame, op_ret, op_errno, loc->inode, &stbuf);

        return 0;
}

int32_t 
posix_mkdir (call_frame_t *frame, xlator_t *this,
             loc_t *loc, mode_t mode)
{
        int32_t     op_ret    = -1;
        int32_t     op_errno  = 0;
        char *      real_path = NULL;
        struct stat stbuf     = {0, };

        DECLARE_OLD_FS_ID_VAR;
  
        VALIDATE_OR_GOTO (frame, out);
        VALIDATE_OR_GOTO (this, out);
        VALIDATE_OR_GOTO (loc, out);

        SET_FS_ID (frame->root->uid, frame->root->gid);
        MAKE_REAL_PATH (real_path, this, loc->path);

        op_ret = mkdir (real_path, mode);
        if (op_ret == -1) {
                op_errno = errno;
                gf_log (this->name, GF_LOG_WARNING, 
                        "mkdir of %s: %s", loc->path, strerror (op_errno));
                goto out;
        }
  
#ifndef HAVE_SET_FSID
        op_ret = chown (real_path, frame->root->uid, frame->root->gid);
        if (op_ret == -1) {
                op_errno = errno;
                gf_log (this->name, GF_LOG_ERROR,
                        "chown on %s: %s", loc->path, strerror (op_errno));
                goto out;
        }
#endif

        op_ret = lstat (real_path, &stbuf);
        if (op_ret == -1) {
                op_errno = errno;
                gf_log (this->name, GF_LOG_ERROR,
                        "lstat on %s: %s", loc->path, strerror (op_errno));
                goto out;
        }

        op_ret = 0;

 out:  
        SET_TO_OLD_FS_ID ();
  
        frame->root->rsp_refs = NULL;
        STACK_UNWIND (frame, op_ret, op_errno, loc->inode, &stbuf);

        return 0;
}


int32_t 
posix_unlink (call_frame_t *frame, xlator_t *this,
              loc_t *loc)
{
        int32_t op_ret    = -1;
        int32_t op_errno  = 0;
        char *  real_path = NULL;
  
        DECLARE_OLD_FS_ID_VAR;

        VALIDATE_OR_GOTO (frame, out);
        VALIDATE_OR_GOTO (this, out);
        VALIDATE_OR_GOTO (loc, out);

        SET_FS_ID (frame->root->uid, frame->root->gid);
        MAKE_REAL_PATH (real_path, this, loc->path);
  
        op_ret = unlink (real_path);
        if (op_ret == -1) {
                op_errno = errno;
                gf_log (this->name, GF_LOG_WARNING, 
                        "unlink of %s: %s", loc->path, strerror (op_errno));
                goto out;
        }

        op_ret = 0;

 out:  
        SET_TO_OLD_FS_ID ();
        frame->root->rsp_refs = NULL;
        STACK_UNWIND (frame, op_ret, op_errno);

        return 0;
}

int32_t 
posix_remove (const char *path, const struct stat *stat, 
              int32_t typeflag, struct FTW *ftw)
{
        return remove (path);
}

int32_t
posix_rmelem (call_frame_t *frame, xlator_t *this,
              const char *path)
{
        int32_t op_ret    = -1;
        int32_t op_errno  = 0;
        char *  real_path = 0;

        VALIDATE_OR_GOTO (frame, out);
        VALIDATE_OR_GOTO (this, out);
        VALIDATE_OR_GOTO (path, out);

        MAKE_REAL_PATH (real_path, this, path);

        /* 
         * FTW_DEPTH = traverse subdirs first before calling
         *             posix_remove on real_path 
         * FTW_PHYS  = do not follow symlinks
         */

        op_ret = nftw (real_path, posix_remove, 20, FTW_DEPTH|FTW_PHYS);

        if (op_ret == -1) {
                op_errno = errno;
                gf_log (this->name, GF_LOG_WARNING, 
                        "nftw on %s: %s", path, strerror (op_errno));
                goto out;
        }

        op_ret = 0;

 out:
        frame->root->rsp_refs = NULL;
        STACK_UNWIND (frame, op_ret, op_errno);

        return 0;
}

int32_t 
posix_rmdir (call_frame_t *frame, xlator_t *this,
             loc_t *loc)
{
        int32_t op_ret    = -1;
        int32_t op_errno  = 0;
        char *  real_path = 0;

        DECLARE_OLD_FS_ID_VAR;

        VALIDATE_OR_GOTO (frame, out);
        VALIDATE_OR_GOTO (this, out);
        VALIDATE_OR_GOTO (loc, out);
  
        SET_FS_ID (frame->root->uid, frame->root->gid);
        MAKE_REAL_PATH (real_path, this, loc->path);
  
        op_ret = rmdir (real_path);
        op_errno = errno;

	if (op_errno == EEXIST)
		/* Solaris sets errno = EEXIST instead of ENOTEMPTY */
		op_errno = ENOTEMPTY;
	  
        if (op_ret == -1 && op_errno != ENOTEMPTY) {
                gf_log (this->name, GF_LOG_WARNING, 
                        "rmdir of %s: %s", loc->path, strerror (op_errno));
                goto out;
        }

 out:
        SET_TO_OLD_FS_ID ();
        frame->root->rsp_refs = NULL;
        STACK_UNWIND (frame, op_ret, op_errno);

        return 0;
}

int32_t 
posix_symlink (call_frame_t *frame, xlator_t *this,
               const char *linkname, loc_t *loc)
{
        int32_t     op_ret    = -1;
        int32_t     op_errno  = 0;
        char *      real_path = 0;
        struct stat stbuf     = { 0, };

        DECLARE_OLD_FS_ID_VAR;

        VALIDATE_OR_GOTO (frame, out);
        VALIDATE_OR_GOTO (this, out);
        VALIDATE_OR_GOTO (linkname, out);
        VALIDATE_OR_GOTO (loc, out);

        SET_FS_ID (frame->root->uid, frame->root->gid);
        MAKE_REAL_PATH (real_path, this, loc->path);
    
        op_ret = symlink (linkname, real_path);

        if (op_ret == -1) {
                op_errno = errno;
                gf_log (this->name, GF_LOG_WARNING, 
                        "symlink of %s --> %s: %s", 
                        loc->path, linkname, strerror (op_errno));
                goto out;
        }
  
#ifndef HAVE_SET_FSID
        op_ret = lchown (real_path, frame->root->uid, frame->root->gid);
        if (op_ret == -1) {
                op_errno = errno;
                gf_log (this->name, GF_LOG_WARNING, 
                        "lchown failed on %s: %s", 
                        loc->path, strerror (op_errno));
                goto out;
        }
#endif
        op_ret = lstat (real_path, &stbuf);
        if (op_ret == -1) {
                op_errno = errno;
                gf_log (this->name, GF_LOG_WARNING, 
                        "lstat failed on %s: %s", 
                        loc->path, strerror (op_errno));
                goto out;
        }

        op_ret = 0;

 out:  
        SET_TO_OLD_FS_ID ();

        frame->root->rsp_refs = NULL;
        STACK_UNWIND (frame, op_ret, op_errno, loc->inode, &stbuf);

        return 0;
}


int
posix_rename (call_frame_t *frame, xlator_t *this,
              loc_t *oldloc, loc_t *newloc)
{
        int32_t     op_ret       = -1;
        int32_t     op_errno     = 0;
        char *      real_oldpath = NULL;
        char *      real_newpath = NULL;
        struct stat stbuf        = {0, };

        DECLARE_OLD_FS_ID_VAR;

        VALIDATE_OR_GOTO (frame, out);
        VALIDATE_OR_GOTO (this, out);
        VALIDATE_OR_GOTO (oldloc, out);
        VALIDATE_OR_GOTO (newloc, out);
  
        SET_FS_ID (frame->root->uid, frame->root->gid);
        MAKE_REAL_PATH (real_oldpath, this, oldloc->path);
        MAKE_REAL_PATH (real_newpath, this, newloc->path);
  
        op_ret = rename (real_oldpath, real_newpath);
        if (op_ret == -1) {
                op_errno = errno;
                gf_log (this->name, GF_LOG_WARNING, 
                        "rename of %s to %s failed: %s", 
                        oldloc->path, newloc->path, strerror (op_errno));
                goto out;
        }
    
        op_ret = lstat (real_newpath, &stbuf);
        if (op_ret == -1) {
                op_errno = errno;
                gf_log (this->name, GF_LOG_WARNING, 
                        "lstat on %s failed: %s", 
                        real_newpath, strerror (op_errno));
                goto out;
        }

        op_ret = 0;

 out:
        SET_TO_OLD_FS_ID ();

        frame->root->rsp_refs = NULL;
        STACK_UNWIND (frame, op_ret, op_errno, &stbuf);

        return 0;
}


int
posix_link (call_frame_t *frame, xlator_t *this,
            loc_t *oldloc, loc_t *newloc)
{
        int32_t     op_ret       = -1;
        int32_t     op_errno     = 0;
        char *      real_oldpath = 0;
        char *      real_newpath = 0;
        struct stat stbuf        = {0, };


        DECLARE_OLD_FS_ID_VAR;
    
        VALIDATE_OR_GOTO (frame, out);
        VALIDATE_OR_GOTO (this, out);
        VALIDATE_OR_GOTO (oldloc, out);
        VALIDATE_OR_GOTO (newloc, out);
  
        SET_FS_ID (frame->root->uid, frame->root->gid);
        MAKE_REAL_PATH (real_oldpath, this, oldloc->path);
        MAKE_REAL_PATH (real_newpath, this, newloc->path);

        op_ret = link (real_oldpath, real_newpath);
        if (op_ret == -1) {
                op_errno = errno;
                gf_log (this->name, GF_LOG_WARNING, 
                        "link %s to %s failed: %s", 
                        oldloc->path, newloc->path, strerror (op_errno));
                goto out;
        }
    
        op_ret = lstat (real_newpath, &stbuf);
        if (op_ret == -1) {
                op_errno = errno;
                gf_log (this->name, GF_LOG_WARNING, 
                        "lstat on %s failed: %s", 
                        real_newpath, strerror (op_errno));
                goto out;
        }

        op_ret = 0;

 out:
        SET_TO_OLD_FS_ID ();

        frame->root->rsp_refs = NULL;
        STACK_UNWIND (frame, op_ret, op_errno, oldloc->inode, &stbuf);

        return 0;
}


int
posix_chmod (call_frame_t *frame, xlator_t *this,
             loc_t *loc, mode_t mode)
{
        int32_t     op_ret    = -1;
        int32_t     op_errno  = 0;
        char *      real_path = 0;
        struct stat stbuf     = {0,};

        DECLARE_OLD_FS_ID_VAR;
  
        VALIDATE_OR_GOTO (frame, out);
        VALIDATE_OR_GOTO (this, out);
        VALIDATE_OR_GOTO (loc, out);
  
        SET_FS_ID (frame->root->uid, frame->root->gid);
        MAKE_REAL_PATH (real_path, this, loc->path);
        
        if (S_ISLNK (loc->inode->st_mode)) {
                /* chmod on a link should always succeed */
                op_ret = 0;
		op_ret = lstat (real_path, &stbuf);
		if (op_ret == -1) {
			op_errno = errno;
			gf_log (this->name, GF_LOG_ERROR, "lstat on %s failed: %s",
				real_path, strerror (op_errno));
			goto out;
		}
                goto out;
        }

        op_ret = lchmod (real_path, mode);
        if ((op_ret == -1) && (errno == ENOSYS)) {
                gf_log (this->name, GF_LOG_DEBUG, 
                        "lchmod not implemented, falling back to chmod");
                op_ret = chmod (real_path, mode);
        }

        if (op_ret == -1) {
                op_errno = errno;
                gf_log (this->name, GF_LOG_ERROR, "chmod on %s failed: %s",
                        loc->path, strerror (op_errno));
                goto out;
        }
        
        op_ret = lstat (real_path, &stbuf);
        if (op_ret == -1) {
                op_errno = errno;
                gf_log (this->name, GF_LOG_ERROR, "lstat on %s failed: %s",
                        real_path, strerror (op_errno));
                goto out;
        }

        op_ret = 0;

 out:
        SET_TO_OLD_FS_ID ();

        frame->root->rsp_refs = NULL;
        STACK_UNWIND (frame, op_ret, op_errno, &stbuf);

        return 0;
}


int
posix_chown (call_frame_t *frame, xlator_t *this,
             loc_t *loc, uid_t uid, gid_t gid)
{
        int32_t     op_ret     = -1;
        int32_t     op_errno   = 0;
        char *      real_path  = 0;
        struct stat stbuf      = {0,};

        DECLARE_OLD_FS_ID_VAR;

        VALIDATE_OR_GOTO (frame, out);
        VALIDATE_OR_GOTO (this, out);
        VALIDATE_OR_GOTO (loc, out);

        SET_FS_ID (frame->root->uid, frame->root->gid);
        MAKE_REAL_PATH (real_path, this, loc->path);
    
        op_ret = lchown (real_path, uid, gid);
        if (op_ret == -1) {
                op_errno = errno;
                gf_log (this->name, GF_LOG_WARNING, "lchown on %s failed: %s",
                        loc->path, strerror (op_errno));
                goto out;
        }
    
        op_ret = lstat (real_path, &stbuf);
        if (op_ret == -1) {
                op_errno = errno;
                gf_log (this->name, GF_LOG_WARNING, "lstat on %s failed: %s", 
                        real_path, strerror (op_errno));
                goto out;
        }

        op_ret = 0;

 out:
        SET_TO_OLD_FS_ID ();
        frame->root->rsp_refs = NULL;  
        STACK_UNWIND (frame, op_ret, op_errno, &stbuf);

        return 0;
}


int32_t 
posix_truncate (call_frame_t *frame,
                xlator_t *this,
                loc_t *loc,
                off_t offset)
{
        int32_t     op_ret    = -1;
        int32_t     op_errno  = 0;
        char *      real_path = 0;
        struct stat stbuf     = {0,};

        DECLARE_OLD_FS_ID_VAR;

        VALIDATE_OR_GOTO (frame, out);
        VALIDATE_OR_GOTO (this, out);
        VALIDATE_OR_GOTO (loc, out);

        SET_FS_ID (frame->root->uid, frame->root->gid);
        MAKE_REAL_PATH (real_path, this, loc->path);

        op_ret = truncate (real_path, offset);
        if (op_ret == -1) {
                op_errno = errno;
                gf_log (this->name, GF_LOG_WARNING, 
                        "truncate on %s failed: %s", 
                        loc->path, strerror (op_errno));
                goto out;
        }
    
        lstat (real_path, &stbuf);
        if (op_ret == -1) {
                op_errno = errno;
                gf_log (this->name, GF_LOG_WARNING, "lstat on %s failed: %s", 
                        real_path, strerror (op_errno));
                goto out;
        }

        op_ret = 0;

 out:
        SET_TO_OLD_FS_ID ();

        frame->root->rsp_refs = NULL;
        STACK_UNWIND (frame, op_ret, op_errno, &stbuf);

        return 0;
}


int
posix_utimens (call_frame_t *frame, xlator_t *this,
               loc_t *loc, struct timespec ts[2])
{
        int32_t        op_ret    = -1;
        int32_t        op_errno  = 0;
        char *         real_path = 0;
        struct stat    stbuf     = {0,};
        struct timeval tv[2]     = {{0,},{0,}};

        DECLARE_OLD_FS_ID_VAR;
  
        VALIDATE_OR_GOTO (frame, out);
        VALIDATE_OR_GOTO (this, out);
        VALIDATE_OR_GOTO (loc, out);
  
        SET_FS_ID (frame->root->uid, frame->root->gid);
        MAKE_REAL_PATH (real_path, this, loc->path);

        tv[0].tv_sec  = ts[0].tv_sec;
        tv[0].tv_usec = ts[0].tv_nsec / 1000;
        tv[1].tv_sec  = ts[1].tv_sec;
        tv[1].tv_usec = ts[1].tv_nsec / 1000;

        op_ret = lutimes (real_path, tv);
        if ((op_ret == -1) && (errno == ENOSYS)) {
                op_ret = utimes (real_path, tv);
        }

        if (op_ret == -1) {
                op_errno = errno;
                gf_log (this->name, GF_LOG_WARNING, 
                        "utimes on %s: %s", real_path, strerror (op_errno));
                goto out;
        }

        op_ret = lstat (real_path, &stbuf);
        if (op_ret == -1) {
                op_errno = errno;
                gf_log (this->name, GF_LOG_WARNING, 
                        "lstat on %s: %s", real_path, strerror (op_errno));
                goto out;
        }

        op_ret = 0;

 out:
        SET_TO_OLD_FS_ID ();

        frame->root->rsp_refs = NULL;
        STACK_UNWIND (frame, op_ret, op_errno, &stbuf);

        return 0;
}

int32_t 
posix_create (call_frame_t *frame, xlator_t *this,
              loc_t *loc, int32_t flags, mode_t mode,
              fd_t *fd)
{
        int32_t                op_ret    = -1;
        int32_t                op_errno  = 0;
        int32_t                _fd       = -1;
        int                    _flags    = 0;
        char *                 real_path = NULL;
        struct stat            stbuf     = {0, };
        struct posix_fd *      pfd       = NULL;
        struct posix_private * priv      = NULL;

        DECLARE_OLD_FS_ID_VAR;

        VALIDATE_OR_GOTO (frame, out);
        VALIDATE_OR_GOTO (this, out);
        VALIDATE_OR_GOTO (this->private, out);
        VALIDATE_OR_GOTO (loc, out);
        VALIDATE_OR_GOTO (fd, out);

        priv = this->private;

        SET_FS_ID (frame->root->uid, frame->root->gid);
        MAKE_REAL_PATH (real_path, this, loc->path);
    
        if (!flags) {
                _flags = O_CREAT | O_RDWR | O_EXCL;
        }
        else {
                _flags = flags | O_CREAT;
        }

        if (priv->o_direct) 
                flags |= O_DIRECT;

        _fd = open (real_path, _flags, mode);

        if (_fd == -1) {
                op_errno = errno;
                gf_log (this->name, GF_LOG_WARNING, 
                        "open on %s: %s", loc->path, strerror (op_errno));
                goto out;
        }
    
#ifndef HAVE_SET_FSID
        op_ret = chown (real_path, frame->root->uid, frame->root->gid);
        if (_fd == -1) {
                op_errno = errno;
                gf_log (this->name, GF_LOG_WARNING, 
                        "chown on %s failed: %s", real_path, strerror (op_errno));
                goto out;
        }
#endif

        op_ret = fstat (_fd, &stbuf);
        if (op_ret == -1) {
                op_errno = errno;
                gf_log (this->name, GF_LOG_WARNING, 
                        "fstat on %d failed: %s", _fd, strerror (op_errno));
                goto out;
        }

        pfd = calloc (1, sizeof (*pfd));

        if (!pfd) {
                op_errno = errno;
                gf_log (this->name, GF_LOG_ERROR,
                        "out of memory :(");
                close (_fd);
                goto out;
        }

        pfd->flags = flags;
        pfd->fd    = _fd;

        dict_set (fd->ctx, this->name, data_from_static_ptr (pfd));

        ((struct posix_private *)this->private)->stats.nr_files++;

        op_ret = 0;

 out:
        SET_TO_OLD_FS_ID ();

        frame->root->rsp_refs = NULL;
        STACK_UNWIND (frame, op_ret, op_errno, fd, loc->inode, &stbuf);

        return 0;
}

int32_t 
posix_open (call_frame_t *frame, xlator_t *this,
            loc_t *loc, int32_t flags, fd_t *fd)
{
        int32_t                op_ret    = -1;
        int32_t                op_errno  = 0;
        char *                 real_path = NULL;
        int32_t                _fd       = -1;
        struct posix_fd *      pfd       = NULL;
        struct posix_private * priv      = NULL;

        DECLARE_OLD_FS_ID_VAR;
    
        VALIDATE_OR_GOTO (frame, out);
        VALIDATE_OR_GOTO (this, out);
        VALIDATE_OR_GOTO (this->private, out);
        VALIDATE_OR_GOTO (loc, out);
        VALIDATE_OR_GOTO (fd, out);

        priv = this->private;

        SET_FS_ID (frame->root->uid, frame->root->gid);
        MAKE_REAL_PATH (real_path, this, loc->path);

        if (priv->o_direct)
                flags |= O_DIRECT;

        _fd = open (real_path, flags, 0);
        if (_fd == -1) {
                op_errno = errno;
                gf_log (this->name, GF_LOG_WARNING, 
                        "open on %s: %s", real_path, strerror (op_errno));
                goto out;
        }
    
        pfd = calloc (1, sizeof (*pfd));

        if (!pfd) {
                op_errno = errno;
                gf_log (this->name, GF_LOG_ERROR,
                        "out of memory :(");
                goto out;
        }

        pfd->flags = flags;
        pfd->fd    = _fd;

        dict_set (fd->ctx, this->name, data_from_static_ptr (pfd));

        ((struct posix_private *)this->private)->stats.nr_files++;

#ifndef HAVE_SET_FSID
        if (flags & O_CREAT) {
                op_ret = chown (real_path, frame->root->uid, frame->root->gid);
                if (op_ret == -1) {
                        op_errno = errno;
                        gf_log (this->name, GF_LOG_WARNING, 
                                "chown on %s failed: %s", real_path, strerror (op_errno));
                        goto out;
                }
        }
#endif

        op_ret = 0;

 out:
        if (op_ret == -1) {
                if (_fd != -1) {
                        close (_fd);
                        _fd = -1;
                }
        }

        SET_TO_OLD_FS_ID ();

        frame->root->rsp_refs = NULL;
        STACK_UNWIND (frame, op_ret, op_errno, fd);

        return 0;
}

#define ALIGN_BUF(ptr,bound) ((void *)((unsigned long)(ptr + bound - 1) & \
                                       (unsigned long)(~(bound - 1))))

int
posix_readv (call_frame_t *frame, xlator_t *this,
             fd_t *fd, size_t size, off_t offset)
{
        int32_t                op_ret     = -1;
        int32_t                op_errno   = 0;
        char *                 buf        = NULL;
        char *                 alloc_buf  = NULL;
        int                    _fd        = -1;
        struct posix_private * priv       = NULL;
        dict_t *               reply_dict = NULL;
        struct iovec           vec        = {0,};
        struct posix_fd *      pfd        = NULL;
        struct stat            stbuf      = {0,};
        int                    align      = 1;
        int                    ret        = -1;
        int                    dict_ret   = -1;

        VALIDATE_OR_GOTO (frame, out);
        VALIDATE_OR_GOTO (this, out);
        VALIDATE_OR_GOTO (fd, out);
        VALIDATE_OR_GOTO (this->private, out);

        priv = this->private;

        ret = dict_get_ptr (fd->ctx, this->name, (void **)&pfd);

        if (ret < 0) {
                op_errno = -ret;
                gf_log (this->name, GF_LOG_ERROR, "pfd is NULL from fd=%p", fd);
                goto out;
        }

        if (!size) {
                op_errno = EINVAL;
                gf_log (this->name, GF_LOG_ERROR, "size == 0");
                goto out;
        }

        if (pfd->flags & O_DIRECT) {
                align = 4096;    /* align to page boundary */
        }

        alloc_buf = malloc (1 * (size + align));
        if (!alloc_buf) {
                op_errno = errno;
                gf_log (this->name, GF_LOG_ERROR,
                        "out of memory :(");
                goto out;
        }

        /* page aligned buffer */
        buf = ALIGN_BUF (alloc_buf, align);

        _fd = pfd->fd;

        op_ret = lseek (_fd, offset, SEEK_SET);
        if (op_ret == -1) {
                op_errno = errno;
                gf_log (this->name, GF_LOG_ERROR, "lseek(%"PRId64") failed: %s", 
                        offset, strerror (op_errno)); 
                goto out;
        }
  
        op_ret = read (_fd, buf, size);
        if (op_ret == -1) {
                op_errno = errno;
                gf_log (this->name, GF_LOG_WARNING, 
                        "read failed: %s", strerror (op_errno));
                goto out;
        }

        priv->read_value    += size;
        priv->interval_read += size;

        vec.iov_base = buf;
        vec.iov_len  = op_ret;
    
        reply_dict = get_new_dict ();
        if (!reply_dict) {
                gf_log (this->name, GF_LOG_ERROR,
                        "out of memory :(");
                goto out;
        }
        dict_ref (reply_dict);

        dict_ret = dict_set_ptr (reply_dict, NULL, alloc_buf);
        if (dict_ret < 0) {
                op_errno = -dict_ret;
                gf_log (this->name, GF_LOG_ERROR, "could not dict_set: (%s)",
                        strerror (op_errno));
                goto out;
        }

        /* 
         *  readv successful, and we need to get the stat of the file
         *  we read from
         */

        ret = fstat (_fd, &stbuf);
        if (ret == -1) {
                op_errno = errno;
                gf_log (this->name, GF_LOG_WARNING, 
                        "fstat failed: %s", strerror (op_errno));
                goto out;
        }

 out:
        if (op_ret == -1) {
                frame->root->rsp_refs = NULL;

                if (reply_dict) {
                        dict_unref (reply_dict);
                        reply_dict = NULL;
                }

                if ((alloc_buf != NULL) && (dict_ret != -1))
                        FREE (alloc_buf);
        }

        if (reply_dict)
                frame->root->rsp_refs = reply_dict;

        STACK_UNWIND (frame, op_ret, op_errno, &vec, 1, &stbuf);

        if (reply_dict)
                dict_unref (reply_dict);

        return 0;
}


int32_t 
posix_writev (call_frame_t *frame, xlator_t *this,
              fd_t *fd, struct iovec *vector, int32_t count, off_t offset)
{
        int32_t                op_ret   = -1;
        int32_t                op_errno = 0;
        int                    _fd      = -1;
        struct posix_private * priv     = NULL;
        struct posix_fd *      pfd      = NULL;
        struct stat            stbuf    = {0,};
        int                      ret      = -1;

        int    idx          = 0;
        int    align        = 4096;
        int    max_buf_size = 0;
        int    retval       = 0;
        char * buf          = NULL; 
        char * alloc_buf    = NULL;

        VALIDATE_OR_GOTO (frame, out);
        VALIDATE_OR_GOTO (this, out);
        VALIDATE_OR_GOTO (fd, out);
        VALIDATE_OR_GOTO (vector, out);
        VALIDATE_OR_GOTO (this->private, out);
        VALIDATE_OR_GOTO (fd->ctx, out);
  
        priv = this->private;
  
        VALIDATE_OR_GOTO (priv, out);

        ret = dict_get_ptr (fd->ctx, this->name, (void **)&pfd); 

        if (ret < 0) {
                gf_log (this->name, GF_LOG_ERROR, "pfd is NULL from fd=%p", fd);
                op_errno = -ret;
                goto out;
        }

        _fd = pfd->fd;

        op_ret = lseek (_fd, offset, SEEK_SET);

        if (op_ret == -1) {
                op_errno = errno;
                gf_log (this->name, GF_LOG_ERROR, "lseek(%"PRId64") failed: %s", 
                        offset, strerror (op_errno));
                goto out;
        }

        /* Check for the O_DIRECT flag during open() */
        if (pfd->flags & O_DIRECT) {
                /* This is O_DIRECT'd file */

                for (idx = 0; idx < count; idx++) {
                        if (max_buf_size < vector[idx].iov_len)
                                max_buf_size = vector[idx].iov_len;
                }

                alloc_buf = malloc (1 * (max_buf_size + align));
                if (!alloc_buf) {
                        op_errno = errno;
                        gf_log (this->name, GF_LOG_ERROR,
                                "out of memory :(");
                        goto out;
                }

                for (idx = 0; idx < count; idx++) {
                        /* page aligned buffer */
                        buf = ALIGN_BUF (alloc_buf, align);

                        memcpy (buf, vector[idx].iov_base, vector[idx].iov_len);
      
                        /* not sure whether writev works on O_DIRECT'd fd */
                        retval = write (_fd, buf, vector[idx].iov_len);
      
                        if (retval == -1) {
                                if (op_ret == -1) {
                                        op_errno = errno;
                                        gf_log (this->name, GF_LOG_WARNING, 
                                                "O_DIRECT enabled: %s", strerror (op_errno));
                                        goto out;
                                }

                                break;
                        }
                        op_ret += retval;
                }

        } else /* if (O_DIRECT) */ {

                /* This is not O_DIRECT'd fd */
                op_ret   = writev (_fd, vector, count);
                if (op_ret == -1) {
                        op_errno = errno;
                        gf_log (this->name, GF_LOG_WARNING, "writev failed: %s", 
                                strerror (op_errno));
                        goto out;
                }
        }

        priv->write_value    += op_ret;
        priv->interval_write += op_ret;
  
        if (op_ret >= 0) {
                /* wiretv successful, we also need to get the stat of 
                 * the file we wrote to 
                 */
                ret = fstat (_fd, &stbuf);
                if (ret == -1) {
                        op_errno = errno;
                        gf_log (this->name, GF_LOG_ERROR, "fstat failed: %s", 
                                strerror (op_errno));
                        goto out;
                }
        }

 out:
        if (alloc_buf) {
                FREE (alloc_buf);
        }

        frame->root->rsp_refs = NULL;
        STACK_UNWIND (frame, op_ret, op_errno, &stbuf);

        return 0;
}


int32_t 
posix_statfs (call_frame_t *frame, xlator_t *this,
              loc_t *loc)
{
        char *                 real_path = NULL;
        int32_t                op_ret    = -1;
        int32_t                op_errno  = 0;
        struct statvfs         buf       = {0, };
        struct posix_private * priv      = NULL;

        VALIDATE_OR_GOTO (frame, out);
        VALIDATE_OR_GOTO (this, out);
        VALIDATE_OR_GOTO (loc, out);
        VALIDATE_OR_GOTO (this->private, out);

        MAKE_REAL_PATH (real_path, this, loc->path);

        priv = this->private;

        op_ret = statvfs (real_path, &buf);

        if (op_ret == -1) {
                op_errno = errno;
                gf_log (this->name, GF_LOG_ERROR, "statvfs failed: %s", 
                        strerror (op_errno));
                goto out;
        }
  
        if (!priv->export_statfs) {
                buf.f_blocks = 0;
                buf.f_bfree  = 0;
                buf.f_bavail = 0;
                buf.f_files  = 0;
                buf.f_ffree  = 0;
                buf.f_favail = 0;
        }

        op_ret = 0;

 out:
        frame->root->rsp_refs = NULL;
        STACK_UNWIND (frame, op_ret, op_errno, &buf);
        return 0;
}


int32_t 
posix_flush (call_frame_t *frame, xlator_t *this,
             fd_t *fd)
{
        int32_t           op_ret   = -1;
        int32_t           op_errno = 0;
        int               _fd      = -1;
        struct posix_fd * pfd      = NULL;
        int               ret      = -1;

        VALIDATE_OR_GOTO (frame, out);
        VALIDATE_OR_GOTO (this, out);
        VALIDATE_OR_GOTO (fd, out);

        ret = dict_get_ptr (fd->ctx, this->name, (void **)&pfd);

        if (ret < 0) {
                op_errno = -ret;
                gf_log (this->name, GF_LOG_ERROR,
                        "pfd is NULL on fd=%p", fd);
                goto out;
        }

        _fd = pfd->fd;
        /* do nothing */
        op_ret = 0;

 out:
        frame->root->rsp_refs = NULL;
        STACK_UNWIND (frame, op_ret, op_errno);

        return 0;
}

int32_t 
posix_close (xlator_t *this,
             fd_t *fd)
{
        int32_t                op_ret   = -1;
        int32_t                op_errno = 0;
        int                    _fd      = -1;
        struct posix_private * priv     = NULL;
        struct posix_fd *      pfd      = NULL;
        int                    ret      = -1;

        VALIDATE_OR_GOTO (this, out);
        VALIDATE_OR_GOTO (fd, out);

        priv = this->private;

        priv->stats.nr_files--;

        ret = dict_get_ptr (fd->ctx, this->name, (void **)&pfd);
        if (ret < 0) {
                op_errno = -ret;
                gf_log (this->name, GF_LOG_ERROR,
                        "pfd is NULL from fd=%p", fd);
                goto out;
        }

        _fd = pfd->fd;

        op_ret = close (_fd);
        if (op_ret == -1) {
                op_errno = errno;
                gf_log (this->name, GF_LOG_WARNING, 
                        "close(): %s", strerror (op_errno));
		goto out;
        }

        if (pfd->dir) {
                op_errno = EBADF;
                gf_log (this->name, GF_LOG_ERROR,
                        "pfd->dir is %p (not NULL) for file fd=%p",
                        pfd->dir, fd);
                goto out;
        }

        op_ret = 0;

 out:
	if (pfd)
		FREE (pfd);

        return 0;
}


int32_t 
posix_fsync (call_frame_t *frame, xlator_t *this,
             fd_t *fd, int32_t datasync)
{
        int32_t           op_ret   = -1;
        int32_t           op_errno = 0;
        int               _fd      = -1;
        struct posix_fd * pfd      = NULL;
        int               ret      = -1;

        DECLARE_OLD_FS_ID_VAR;

        VALIDATE_OR_GOTO (frame, out);
        VALIDATE_OR_GOTO (this, out);
        VALIDATE_OR_GOTO (fd, out);
  
        SET_FS_ID (frame->root->uid, frame->root->gid);

#ifdef GF_DARWIN_HOST_OS
        /* Always return success in case of fsync in MAC OS X */
        op_ret = 0;
        goto out;
#endif 

        ret = dict_get_ptr (fd->ctx, this->name, (void **)&pfd);
        if (ret < 0) {
                op_errno = -ret;
                gf_log (this->name, GF_LOG_ERROR, "pfd not found in fd->ctx");
                goto out;
        }

        _fd = pfd->fd;

        if (datasync) {
                ;
#ifdef HAVE_FDATASYNC
                op_ret = fdatasync (_fd);
#endif
        } else {
                op_ret = fsync (_fd);
                if (op_ret == -1) {
                        op_errno = errno;
                        gf_log (this->name, GF_LOG_WARNING, "fsync: %s", 
                                strerror (op_errno));
                }
        }

        op_ret = 0;

 out:
        SET_TO_OLD_FS_ID ();
        frame->root->rsp_refs = NULL;

        STACK_UNWIND (frame, op_ret, op_errno);
  
        return 0;
}

static int gf_posix_xattr_enotsup_log;

int
posix_incver (call_frame_t *frame, xlator_t *this,
              const char *path, fd_t *fd)
{
        char *            real_path   = NULL;
        char              version[50] = {0,};
        int               size        = 0;
        int               ret         = -1;
        int32_t           op_ret      = -1;
        int32_t           op_errno    = 0;
        int32_t           ver         = 1;
        int               _fd         = 0;
        struct posix_fd * pfd         = NULL;

        VALIDATE_OR_GOTO (frame, out);
        VALIDATE_OR_GOTO (this, out);
        VALIDATE_OR_GOTO (path, out);

        MAKE_REAL_PATH (real_path, this, path);

        if (fd) {
                ret = dict_get_ptr (fd->ctx, this->name, (void **)&pfd);
      
                if (ret < 0) {
                        op_errno = -ret;
                        gf_log (this->name, GF_LOG_WARNING, "pfd NULL!");
                        return 0;
                }
        }

        if (fd) {
                _fd = pfd->fd;
                size = fgetxattr (_fd, GLUSTERFS_VERSION, version, 50);
        }
        else {
                size = lgetxattr (real_path, GLUSTERFS_VERSION, version, 50);
        }

        op_errno = errno;
        if ((size == -1) && ((op_errno != ENODATA) && (op_errno != ENOENT))) {
                if (op_errno == ENOTSUP) {
                        GF_LOG_OCCASIONALLY (gf_posix_xattr_enotsup_log,
                                             this->name, GF_LOG_WARNING, 
                                             "Extended attributes not supported. "); 
                } 
                else {
                        gf_log (this->name, GF_LOG_WARNING, "%s: %s", 
                                path, strerror(op_errno));
                }
		goto out;
        }

        else if (size > 0) {
                version[size] = '\0';
                ver = strtoll (version, NULL, 10);
        }
  
        ver++;
        sprintf (version, "%u", ver);

        if (fd)
                op_ret = fsetxattr (_fd, GLUSTERFS_VERSION, version, 
                                    strlen (version), 0);
        else
                op_ret = lsetxattr (real_path, GLUSTERFS_VERSION, version, 
                                    strlen (version), 0);

        if (op_ret == -1) {
                op_errno = errno;
		if (op_errno != ENOENT) {
			gf_log (this->name, GF_LOG_ERROR, 
				"setxattr failed on %s: %s", 
				real_path, strerror (op_errno));
		}
		goto out;
        }

        op_ret = 0;

 out:
        STACK_UNWIND (frame, op_ret, op_errno);
        return 0;
}

int
set_file_contents (xlator_t *this, char *real_path, 
                   data_pair_t *trav, int flags)
{
        char *      key                        = NULL;
        char        real_filepath[GF_PATH_MAX] = {0,};
        int32_t     file_fd                    = -1;
        int         op_ret                     = 0;
        int         ret                        = -1;
        
        key = &(trav->key[15]);
        sprintf (real_filepath, "%s/%s", real_path, key);

        if (flags & XATTR_REPLACE) {
                /* if file exists, replace it 
                 * else, error out */
                file_fd = open (real_filepath, O_TRUNC|O_WRONLY);

                if (file_fd == -1) {
                        goto create;
                }
        
                if (trav->value->len) {
                        ret = write (file_fd, trav->value->data, trav->value->len);
                        if (ret == -1) {
                                op_ret = -errno;
                                gf_log (this->name, GF_LOG_ERROR, "write failed "
                                        "while doing setxattr for key %s on path %s: %s",
                                        key, real_filepath, strerror (errno));
                                goto out;
                        }
          
                        ret = close (file_fd);
                        if (ret == -1) {
                                op_ret = -errno;
                                gf_log (this->name, GF_LOG_ERROR,
                                        "close failed on %s: %s", 
                                        real_filepath, strerror (errno));
                                goto out;
                        }
                }
        
        create: /* we know file doesn't exist, create it */

                file_fd = open (real_filepath, O_CREAT|O_WRONLY, 0644);

                if (file_fd == -1) {
                        op_ret = -errno;
                        gf_log (this->name, GF_LOG_ERROR,
                                "failed to open file %s with O_CREAT: %s", 
                                key, strerror (errno));
                        goto out;
                }
        
                ret = write (file_fd, trav->value->data, trav->value->len);
                if (ret == -1) {
                        op_ret = -errno;
                        gf_log (this->name, GF_LOG_ERROR,
                                "write failed on %s while setxattr with key %s: %s", 
                                real_filepath, key, strerror (errno));
                        goto out;
                }

                ret = close (file_fd);
                if (ret == -1) {
                        op_ret = -errno;
                        gf_log (this->name, GF_LOG_ERROR,
                                "close failed on %s while setxattr with key %s: %s", 
                                real_filepath, key, strerror (errno));
                        goto out;
                }
        }

 out:
        return op_ret;
}

int 
handle_pair (xlator_t *this, char *real_path, 
             data_pair_t *trav, int flags)
{
        int sys_ret = -1;
        int ret     = 0;

        if (GF_FILE_CONTENT_REQUEST(trav->key)) {
                ret = set_file_contents (this, real_path, trav, flags);
        } else {
                sys_ret = lsetxattr (real_path, trav->key, trav->value->data, 
                                     trav->value->len, flags);

                if (sys_ret < 0) {
                        if (errno == ENOTSUP) {
                                GF_LOG_OCCASIONALLY (gf_posix_xattr_enotsup_log,
                                                     this->name, GF_LOG_WARNING, 
                                                     "Extended attributes not supported");
                        } else if (errno == ENOENT) {
                                gf_log (this->name, GF_LOG_DEBUG,
                                        "setxattr on %s failed: %s", real_path,
                                        strerror (errno));
                        } else {
                                gf_log (this->name, GF_LOG_WARNING, 
                                        "%s: key:%s error:%s", 
                                        real_path, trav->key, strerror (errno));
                        }

                        ret = -errno;
                        goto out;
                }
        }
 out:
        return ret;
}

int32_t 
posix_setxattr (call_frame_t *frame, xlator_t *this,
                loc_t *loc, dict_t *dict, int flags)
{
        int32_t       op_ret                  = -1;
        int32_t       op_errno                = 0;
        char *        real_path               = NULL;
        data_pair_t * trav                    = NULL;
        int           ret                     = -1;

        DECLARE_OLD_FS_ID_VAR;
        SET_FS_ID (frame->root->uid, frame->root->gid);

        VALIDATE_OR_GOTO (frame, out);
        VALIDATE_OR_GOTO (this, out);
        VALIDATE_OR_GOTO (loc, out);
        VALIDATE_OR_GOTO (dict, out);

        MAKE_REAL_PATH (real_path, this, loc->path);

        trav = dict->members_list;

        while (trav) {
                ret = handle_pair (this, real_path, trav, flags);
                if (ret < 0) {
                        op_errno = -ret;
                        goto out;
                }
                trav = trav->next;
        }

        op_ret = 0;

 out:
        SET_TO_OLD_FS_ID ();
        frame->root->rsp_refs = NULL;
        STACK_UNWIND (frame, op_ret, op_errno);

        return 0;
}

int
get_file_contents (xlator_t *this, char *real_path, 
                   const char *name, char **contents)
{
        char        real_filepath[GF_PATH_MAX] = {0,};
        char *      key                        = NULL;
        int32_t     file_fd                    = -1;
        struct stat stbuf                      = {0,};
        int         op_ret                     = 0;
        int         ret                        = -1;

        key = (char *) &(name[15]);
        sprintf (real_filepath, "%s/%s", real_path, key);

        op_ret = lstat (real_filepath, &stbuf);
        if (op_ret == -1) {
                op_ret = -errno;
                gf_log (this->name, GF_LOG_ERROR, "lstat failed on %s: %s", 
                        real_filepath, strerror (errno));
                goto out;
        }

        file_fd = open (real_filepath, O_RDONLY);

        if (file_fd == -1) {
                op_ret = -errno;
                gf_log (this->name, GF_LOG_ERROR, "open failed on %s: %s",
                        real_filepath, strerror (errno));
                goto out;
        }

        *contents = calloc (stbuf.st_size + 1, sizeof(char));

        if (! *contents) {
                op_ret = -errno;
                gf_log (this->name, GF_LOG_ERROR, "out of memory :(");
                goto out;
        }

        ret = gf_full_read (file_fd, *contents, stbuf.st_size);
        if (ret <= 0) {
                op_ret = -1;
                gf_log (this->name, GF_LOG_ERROR, "read on %s failed",
                        real_filepath);
                goto out;
        }

        *contents[stbuf.st_size] = '\0';

        op_ret = close (file_fd);
        file_fd = -1;
        if (op_ret == -1) {
                op_ret = -errno;
                gf_log (this->name, GF_LOG_ERROR, "close on %s failed: %s",
                        real_filepath, strerror (errno));
                goto out;
        }

 out:
        if (op_ret < 0) {
                if (*contents)
                        FREE (*contents);
                if (file_fd != -1) 
                        close (file_fd);
        }

        return op_ret;
}

/**
 * posix_getxattr - this function returns a dictionary with all the 
 *                  key:value pair present as xattr. used for both 'listxattr' and
 *                  'getxattr'.
 */
int32_t 
posix_getxattr (call_frame_t *frame, xlator_t *this,
                loc_t *loc, const char *name)
{
        int32_t  op_ret         = -1;
        int32_t  op_errno       = ENOENT;
        int32_t  list_offset    = 0;
        size_t   size           = 0;
        size_t   remaining_size = 0;
        char     key[1024]      = {0,};
        char *   value          = NULL;
        char *   list           = NULL;
        char *   real_path      = NULL;
        dict_t * dict           = NULL;
        char *   file_contents  = NULL;
        int      ret            = -1;

        DECLARE_OLD_FS_ID_VAR;

        VALIDATE_OR_GOTO (frame, out);
        VALIDATE_OR_GOTO (this, out);
        VALIDATE_OR_GOTO (loc, out);

        SET_FS_ID (frame->root->uid, frame->root->gid);
        MAKE_REAL_PATH (real_path, this, loc->path);

        if (loc->inode && S_ISDIR(loc->inode->st_mode) && name && 
	    GF_FILE_CONTENT_REQUEST(name)) {
                ret = get_file_contents (this, real_path, name, &file_contents);
                if (ret < 0) {
                        op_errno = -ret;
                        gf_log (this->name, GF_LOG_ERROR, "getting file contents failed: %s",
                                strerror (op_errno));
                        goto out;
                }
        }  

        /* Get the total size */
        dict = get_new_dict ();
        if (!dict) {
                gf_log (this->name, GF_LOG_ERROR, "out of memory :(");
                goto out;
        }

        size = llistxattr (real_path, NULL, 0);
        if (size == -1) {
                op_errno = errno;
                if ((errno == ENOTSUP) || (errno == ENOSYS)) {
                        GF_LOG_OCCASIONALLY (gf_posix_xattr_enotsup_log,
                                             this->name, GF_LOG_WARNING, 
                                             "Extended attributes not supported.");
                }
                else {
                        gf_log (this->name, GF_LOG_ERROR,"listxattr failed on %s: %s", 
                                real_path, strerror (op_errno));
                }
                goto out;
        }
      
        if (size == 0)
                goto done;

        list = alloca (size + 1);
        if (!list) {
                op_errno = errno;
                gf_log (this->name, GF_LOG_ERROR, "out of memory :(");
                goto out;
        }

        size = llistxattr (real_path, list, size);
    
        remaining_size = size;
        list_offset = 0;
        while (remaining_size > 0) {
                if(*(list + list_offset) == '\0')
                        break;

                strcpy (key, list + list_offset);
                op_ret = lgetxattr (real_path, key, NULL, 0);
                if (op_ret == -1)
                        break;

                value = calloc (op_ret + 1, sizeof(char));
                if (!value) {
                        op_errno = errno;
                        gf_log (this->name, GF_LOG_ERROR, "out of memory :(");
                        goto out;
                }

                op_ret = lgetxattr (real_path, key, value, op_ret);
                if (op_ret == -1)
                        break;

                value [op_ret] = '\0';
                dict_set (dict, key, data_from_dynptr (value, op_ret));
                remaining_size -= strlen (key) + 1;
                list_offset += strlen (key) + 1;

        } /* while (remaining_size > 0) */

 done:
        op_ret = size;
        
        if (dict) {
                dict_ref (dict);
        }
        
 out:
        SET_TO_OLD_FS_ID ();
        frame->root->rsp_refs = NULL;
        STACK_UNWIND (frame, op_ret, op_errno, dict);
        
        if (dict)
                dict_unref (dict);
        
        return 0;
}
                     
int32_t 
posix_removexattr (call_frame_t *frame, xlator_t *this,
                   loc_t *loc, const char *name)
{
        int32_t op_ret    = -1;
        int32_t op_errno  = 0;
        char *  real_path = NULL;

        DECLARE_OLD_FS_ID_VAR;

        MAKE_REAL_PATH (real_path, this, loc->path);
  
        SET_FS_ID (frame->root->uid, frame->root->gid);

        op_ret = lremovexattr (real_path, name);

        if (op_ret == -1) {
                op_errno = errno;
                gf_log (this->name, GF_LOG_WARNING, 
                        "removexattr on %s: %s", loc->path, 
                        strerror (op_errno));
                goto out;
        }

        op_ret = 0;

 out:
        SET_TO_OLD_FS_ID ();    

        frame->root->rsp_refs = NULL;  
        STACK_UNWIND (frame, op_ret, op_errno);
        return 0;
}


int32_t 
posix_fsyncdir (call_frame_t *frame, xlator_t *this,
                fd_t *fd, int datasync)
{
        int32_t           op_ret   = -1;
        int32_t           op_errno = 0;
        struct posix_fd * pfd      = NULL;
        int               _fd      = -1;
        int               ret      = -1;

        VALIDATE_OR_GOTO (frame, out);
        VALIDATE_OR_GOTO (this, out);
        VALIDATE_OR_GOTO (fd, out);

        ret = dict_get_ptr (fd->ctx, this->name, (void **)&pfd);

        if (ret < 0) {
                op_errno = -ret;
                gf_log (this->name, GF_LOG_ERROR,
                        "pfd is NULL, fd=%p", fd);
                goto out;
        }

        _fd = pfd->fd;

        op_ret = 0;

 out:
        frame->root->rsp_refs = NULL;
        STACK_UNWIND (frame, op_ret, op_errno);

        return 0;
}


void
posix_print_xattr (dict_t *this,
		   char *key,
		   data_t *value,
		   void *data)
{
	gf_log ("posix", GF_LOG_TRACE,
		"(key/val) = (%s/%d)", key, data_to_int32 (value));
}


/**
 * add_array - add two arrays of 32-bit numbers (stored in network byte order)
 * dest = dest + src
 * @count: number of 32-bit numbers
 * FIXME: handle overflow
 */

static void
add_array (int32_t *dest, int32_t *src, int count)
{
	int i = 0;
	for (i = 0; i < count; i++) {
		dest[i] = hton32 (ntoh32 (dest[i]) + ntoh32 (src[i]));
	}
}

/**
 * xattrop - xattr operations - for internal use by GlusterFS
 * @optype: ADD_ARRAY:
 *            dict should contain:
 *               "key" ==> array of 32-bit numbers in network byte order
 */

int
posix_xattrop (call_frame_t *frame, xlator_t *this,
	       fd_t *fd, const char *path, int32_t optype, dict_t *xattr)
{
	char            *real_path = NULL;
	int32_t         *array = NULL;
	int              size = 0;
	int              count = 0;

	int              op_ret = 0;
	int              op_errno = 0;

	int              _fd = -1;
	data_t          *pfd_data = NULL; 
	struct posix_fd *pfd = NULL;

	data_pair_t     *trav = NULL;

	VALIDATE_OR_GOTO (frame, out);
	VALIDATE_OR_GOTO (xattr, out);
	VALIDATE_OR_GOTO (this, out);

	trav = xattr->members_list;

	MAKE_REAL_PATH (real_path, this, path);

	if (fd) {
		pfd_data = dict_get (fd->ctx, this->name);
		pfd = data_to_ptr (pfd_data);
		if (pfd == NULL) {
			gf_log (this->name, GF_LOG_ERROR,
				"pfd is NULL. path=%s. optype=%d",
				path ? path : "<NULL>", optype);
			op_ret = -1;
			op_errno = EBADFD;
			goto out;
		}
		_fd = pfd->fd;
	}

	while (trav) {
		count = trav->value->len / 4;
		array = calloc (count, sizeof (int32_t));
		
		if (_fd != -1) {
			size = fgetxattr (_fd, trav->key, array, trav->value->len);
		} else {
			size = lgetxattr (real_path, trav->key, array, trav->value->len);
		}

		op_errno = errno;
		if ((size == -1) && (op_errno != ENODATA)) {
			if (op_errno == ENOTSUP) {
                                GF_LOG_OCCASIONALLY (gf_posix_xattr_enotsup_log,
                                                     this->name, GF_LOG_WARNING, 
                                                     "extended attributes not supported by filesystem");
			} else 	{
				gf_log (this->name, GF_LOG_ERROR,
					"%s (%d): %s", path, _fd,
					strerror (op_errno));
			}
			goto out;
		}

		switch (optype) {
		case GF_XATTROP_ADD_ARRAY:
			add_array (array, (int32_t *) trav->value->data, trav->value->len / 4);
			break;
		default:
			gf_log (this->name, GF_LOG_ERROR,
				"unknown xattrop type %d. path=%s",
				optype, path);
			op_ret = -1;
			op_errno = EINVAL;
			goto out;
		}

		if (_fd != -1)
			size = fsetxattr (_fd, trav->key, array,
					  trav->value->len, 0);
		else
			size = lsetxattr (real_path, trav->key, array,
					  trav->value->len, 0);

		op_errno = errno;
		if (size == -1) {
			gf_log (this->name, GF_LOG_ERROR,
				"%s (%d): key=%s (%s)", path, _fd,
				trav->key, strerror (op_errno));
			op_ret = -1;
			goto out;
		}

		FREE (array);
		trav = trav->next;
	}

out:
	STACK_UNWIND (frame, op_ret, op_errno, xattr);
	return 0;
}


int
posix_access (call_frame_t *frame, xlator_t *this,
              loc_t *loc, int32_t mask)
{
        int32_t op_ret    = -1;
        int32_t op_errno  = 0;
        char *  real_path = NULL;

        DECLARE_OLD_FS_ID_VAR;
        SET_FS_ID (frame->root->uid, frame->root->gid);

        VALIDATE_OR_GOTO (frame, out);
        VALIDATE_OR_GOTO (this, out);
        VALIDATE_OR_GOTO (loc, out);

        MAKE_REAL_PATH (real_path, this, loc->path);

        op_ret = access (real_path, mask & 07);
        if (op_ret == -1) {
                op_errno = errno;
                gf_log (this->name, GF_LOG_WARNING, "access failed on %s: %s", 
                        loc->path, strerror (op_errno));
                goto out;
        }

        op_ret = 0;

 out:
        SET_TO_OLD_FS_ID ();

        frame->root->rsp_refs = NULL;  
        STACK_UNWIND (frame, op_ret, op_errno);
        return 0;
}


int32_t 
posix_ftruncate (call_frame_t *frame, xlator_t *this,
                 fd_t *fd, off_t offset)
{
        int32_t           op_ret   = -1;
        int32_t           op_errno = 0;
        int               _fd      = -1;
        struct stat       buf      = {0,};
        struct posix_fd * pfd      = NULL;
        int               ret      = -1;

        DECLARE_OLD_FS_ID_VAR;
        SET_FS_ID (frame->root->uid, frame->root->gid);

        VALIDATE_OR_GOTO (frame, out);
        VALIDATE_OR_GOTO (this, out);
        VALIDATE_OR_GOTO (fd, out);

        ret = dict_get_ptr (fd->ctx, this->name, (void **)&pfd);

        if (ret < 0) {
                gf_log (this->name, GF_LOG_ERROR,
                        "pfd is NULL, fd=%p", fd);
                op_errno = -ret;
                goto out;
        }

        _fd = pfd->fd;

        op_ret = ftruncate (_fd, offset);

        if (op_ret == -1) {
                op_errno = errno;
                gf_log (this->name, GF_LOG_WARNING, "ftruncate failed: %s", 
                        strerror (errno));
                goto out;
        }
    
        op_ret = fstat (_fd, &buf);
        if (op_ret == -1) {
                op_errno = errno;
                gf_log (this->name, GF_LOG_WARNING, "fstat failed: %s", 
                        strerror (errno));
                goto out;
        }

        op_ret = 0;

 out:
        SET_TO_OLD_FS_ID ();

        frame->root->rsp_refs = NULL;
        STACK_UNWIND (frame, op_ret, op_errno, &buf);

        return 0;
}

int32_t 
posix_fchown (call_frame_t *frame, xlator_t *this,
              fd_t *fd, uid_t uid, gid_t gid)
{
        int32_t           op_ret   = -1;
        int32_t           op_errno = 0;
        int               _fd      = -1;
        struct stat       buf      = {0,};
        struct posix_fd * pfd      = NULL;
        int               ret      = -1;

        DECLARE_OLD_FS_ID_VAR;

        SET_FS_ID (frame->root->uid, frame->root->gid);

        VALIDATE_OR_GOTO (frame, out);
        VALIDATE_OR_GOTO (this, out);
        VALIDATE_OR_GOTO (fd, out);

        ret = dict_get_ptr (fd->ctx, this->name, (void **)&pfd);

        if (ret < 0) {
                gf_log (this->name, GF_LOG_ERROR,
                        "pfd is NULL, fd=%p", fd);
                op_errno = -ret;
                goto out;
        }

        _fd = pfd->fd;

        op_ret = fchown (_fd, uid, gid);
        if (op_ret == -1) {
                op_errno = errno;
                gf_log (this->name, GF_LOG_WARNING, "fchown failed: %s", 
                        strerror (op_errno));
                goto out;
        }

        op_ret = fstat (_fd, &buf);
        if (op_ret == -1) {
                op_errno = errno;
                gf_log (this->name, GF_LOG_WARNING, "fstat failed: %s", 
                        strerror (op_errno));
                goto out;
        }
  
        op_ret = 0;

 out:
        SET_TO_OLD_FS_ID ();
        frame->root->rsp_refs = NULL;
        STACK_UNWIND (frame, op_ret, op_errno, &buf);

        return 0;
}


int32_t 
posix_fchmod (call_frame_t *frame, xlator_t *this,
              fd_t *fd, mode_t mode)
{
        int32_t           op_ret   = -1;
        int32_t           op_errno = 0;
        int               _fd      = -1;
        struct stat       buf      = {0,};
        struct posix_fd * pfd      = NULL;
        int               ret      = -1;

        DECLARE_OLD_FS_ID_VAR;

        SET_FS_ID (frame->root->uid, frame->root->gid);

        VALIDATE_OR_GOTO (frame, out);
        VALIDATE_OR_GOTO (this, out);
        VALIDATE_OR_GOTO (fd, out);

        ret = dict_get_ptr (fd->ctx, this->name, (void **)&pfd);

        if (ret < 0) {
                gf_log (this->name, GF_LOG_ERROR,
                        "pfd is NULL fd=%p", fd);
                op_errno = -ret;
                goto out;
        }

        _fd = pfd->fd;

        op_ret = fchmod (_fd, mode);

        if (op_ret == -1) {
                op_errno = errno;
                gf_log (this->name, GF_LOG_WARNING, "fchmod failed: %s", strerror (errno));
                goto out;
        }

        op_ret = fstat (_fd, &buf);
        if (op_ret == -1) {
                op_errno = errno;
                gf_log (this->name, GF_LOG_WARNING, "fstat failed: %s", strerror (errno));
                goto out;
        }
  
        op_ret = 0;

 out:
        SET_TO_OLD_FS_ID ();

        frame->root->rsp_refs = NULL;  
        STACK_UNWIND (frame, op_ret, op_errno, &buf);

        return 0;
}

static int
same_file_type (mode_t m1, mode_t m2)
{
        return (S_ISREG(m1) && S_ISREG(m2)) 
                || (S_ISDIR(m1) && S_ISDIR(m2)) 
                || (S_ISLNK(m1) && S_ISLNK(m2))
                || (S_ISBLK(m1) && S_ISBLK(m2))
                || (S_ISCHR(m1) && S_ISCHR(m2))
                || (S_ISFIFO(m1) && S_ISCHR(m2));
}

static int 
ensure_file_type (xlator_t *this, char *pathname, mode_t mode)
{
        struct stat stbuf  = {0,};
        int         op_ret = 0;
        int         ret    = -1;

        ret = stat (pathname, &stbuf);
        if (ret == -1) {
                op_ret = -errno;
                gf_log (this->name, GF_LOG_CRITICAL,
                        "stat failed while trying to make sure entry %s is a directory: %s", pathname, strerror (errno));
                goto out;
        }

        if (!same_file_type (mode, stbuf.st_mode)) {
                op_ret = -EEXIST;
                gf_log (this->name, GF_LOG_CRITICAL,
                        "entry %s is a different type of file than expected", pathname);
                goto out;
        } 
 out:
        return op_ret;
}

static int 
create_entry (xlator_t *this, int32_t flags,
              dir_entry_t *entry, char *pathname)
{
        int op_ret        = 0;
        int ret           = -1;

        if (S_ISDIR (entry->buf.st_mode)) {
                /* 
                 * If the entry is directory, create it by
                 * calling 'mkdir'. If the entry is already 
                 * present, check if it is a directory, 
                 * and issue a warning if otherwise.
                 */

                ret = mkdir (pathname, entry->buf.st_mode);
                if (ret == -1) {
                        if (errno == EEXIST) {
                                op_ret = ensure_file_type (this, pathname, 
                                                           entry->buf.st_mode);
                        }
                        else {
                                op_ret = -errno;
                                gf_log (this->name, GF_LOG_DEBUG, 
                                        "mkdir %s with mode (0%o) failed: %s", 
                                        pathname, entry->buf.st_mode, 
                                        strerror (errno));
                                goto out;
                        }
                }
                        
        } else if ((flags & GF_SET_IF_NOT_PRESENT) 
                   || !(flags & GF_SET_DIR_ONLY)) {

                /* create a 0-byte file here */

                if (S_ISREG (entry->buf.st_mode)) {
                        ret = open (pathname, O_CREAT|O_EXCL, 
                                    entry->buf.st_mode);

                        if (ret == -1) {
                                if (errno == EEXIST) {
                                        op_ret = ensure_file_type (this, pathname,
                                                                   entry->buf.st_mode);
                                }
                                else {
                                        op_ret = -errno;
                                        gf_log (this->name, GF_LOG_ERROR,
                                                "Error creating file %s with mode (0%o): %s",
                                                pathname, entry->buf.st_mode, 
                                                strerror (errno));
                                        goto out;
                                } 
                        }
                                
                        close (ret);

                } else if (S_ISLNK(entry->buf.st_mode)) {
                        ret = symlink (entry->link, pathname);

                        if (ret == -1) {
                                if (errno == EEXIST) {
                                        op_ret = ensure_file_type (this, pathname,
                                                                   entry->buf.st_mode);
                                }
                                else {
                                        op_ret = -errno;
                                        gf_log (this->name, GF_LOG_ERROR,
                                                "error creating symlink %s: %s", 
                                                pathname, strerror (errno));
                                        goto out;
                                }
                        }

                } else if (S_ISBLK (entry->buf.st_mode) || 
                           S_ISCHR (entry->buf.st_mode) || 
                           S_ISFIFO (entry->buf.st_mode)) {

                        ret = mknod (pathname, entry->buf.st_mode, 
                                     entry->buf.st_dev);

                        if (ret == -1) {
                                if (errno == EEXIST) {
                                        op_ret = ensure_file_type (this, pathname,
                                                                   entry->buf.st_mode);
                                } else {
                                        op_ret = -errno;
                                        gf_log (this->name, GF_LOG_ERROR,
                                                "error creating device file %s: %s",
                                                pathname, strerror (errno));
                                        goto out;
                                }
                        }
                } else {
			gf_log (this->name, GF_LOG_ERROR,
				"invalid mode 0%o for %s", entry->buf.st_mode,
				pathname);
			op_ret = -EINVAL;
			goto out;
		}
        }
 out:
        return op_ret;

}

int32_t 
posix_setdents (call_frame_t *frame, xlator_t *this,
                fd_t *fd, int32_t flags, dir_entry_t *entries,
                int32_t count)
{
        char *            real_path      = NULL;
        char *            entry_path     = NULL;
        int32_t           real_path_len  = -1;
        int32_t           entry_path_len = -1;
        int32_t           ret            = 0;
        int32_t           op_ret         = -1;
        int32_t           op_errno       = 0;
        struct posix_fd * pfd            = {0, };
        struct timeval    tv[2]          = {{0, }, {0, }};

        char              pathname[GF_PATH_MAX] = {0,};
        dir_entry_t *     trav           = NULL;

        VALIDATE_OR_GOTO (frame, out);
        VALIDATE_OR_GOTO (this, out);
        VALIDATE_OR_GOTO (fd, out);
        VALIDATE_OR_GOTO (entries, out);

        tv[0].tv_sec = tv[0].tv_usec = 0;
        tv[1].tv_sec = tv[1].tv_usec = 0;

        ret = dict_get_ptr (fd->ctx, this->name, (void **)&pfd);
        if (ret < 0) {
                op_errno = -ret;
                gf_log (this->name, GF_LOG_ERROR, "fd->ctx not found on fd=%p for %s",
                        fd, this->name);
                goto out;
        }

        real_path = pfd->path;

        if (!real_path) {
                op_errno = EINVAL;
                gf_log (this->name, GF_LOG_ERROR,
                        "path is NULL on pfd=%p fd=%p", pfd, fd);
                goto out;
        }

        real_path_len  = strlen (real_path);
        entry_path_len = real_path_len + 256;
        entry_path     = calloc (1, entry_path_len);

        if (!entry_path) {
                op_errno = errno;
                gf_log (this->name, GF_LOG_ERROR, "out of memory :(");
                goto out;
        }

        strcpy (entry_path, real_path);
        entry_path[real_path_len] = '/';

        /* fd exists, and everything looks fine */
        /**
         * create an entry for each one present in '@entries' 
         *  - if flag is set (ie, if its namespace), create both directories and 
         *    files 
         *  - if not set, create only directories.
         *
         *  after the entry is created, change the mode and ownership of the entry
         *  according to the stat present in entries->buf.  
         */

        trav = entries->next;
        while (trav) {
                strcpy (pathname, entry_path);
                strcat (pathname, trav->name);

                ret = create_entry (this, flags, trav, pathname);
                if (ret < 0) {
                        op_errno = -ret;
                        goto out;
                }

                /* TODO: handle another flag, GF_SET_OVERWRITE */

                /* Change the mode */
                ret = chmod (pathname, trav->buf.st_mode);
                if (ret == -1) {
                        op_errno = errno;
                        gf_log (this->name, GF_LOG_ERROR, 
                                "chmod on %s failed: %s", pathname,
                                strerror (op_errno));
                        goto out;
                }

                /* change the ownership */
                ret = lchown (pathname, trav->buf.st_uid, trav->buf.st_gid);
                if (ret == -1) {
                        op_errno = errno;
                        gf_log (this->name, GF_LOG_ERROR, 
                                "chmod on %s failed: %s", pathname,
                                strerror (op_errno));
                        goto out;
                }

                if (flags & GF_SET_EPOCH_TIME) {
                        ret = utimes (pathname, tv);
                        if (ret == -1) {
                                op_errno = errno;
                                gf_log (this->name, GF_LOG_ERROR,
                                        "utimes on %s failed: %s", pathname,
                                        strerror (op_errno));
                                goto out;
                        }
                }

                /* consider the next entry */
                trav = trav->next;
        }

        op_ret = 0;
 out:
        frame->root->rsp_refs = NULL;
        STACK_UNWIND (frame, op_ret, op_errno);
        if (entry_path)
                FREE (entry_path);

        return 0;
}

int32_t 
posix_fstat (call_frame_t *frame, xlator_t *this,
             fd_t *fd)
{
        int               _fd      = -1;
        int32_t           op_ret   = -1;
        int32_t           op_errno = 0;
        struct stat       buf      = {0,};
        struct posix_fd * pfd      = NULL;
        int               ret      = -1;

        DECLARE_OLD_FS_ID_VAR;
        SET_FS_ID (frame->root->uid, frame->root->gid);

        VALIDATE_OR_GOTO (frame, out);
        VALIDATE_OR_GOTO (this, out);
        VALIDATE_OR_GOTO (fd, out);

        ret = dict_get_ptr (fd->ctx, this->name, (void **)&pfd);

        if (ret < 0) {
                gf_log (this->name, GF_LOG_ERROR,
                        "pfd is NULL, fd=%p", fd);
                op_errno = -ret;
                goto out;
        }

        _fd = pfd->fd;

        op_ret = fstat (_fd, &buf);

        if (op_ret == -1) {
                op_errno = errno;
                gf_log (this->name, GF_LOG_WARNING, "fstat failed: %s", 
                        strerror (op_errno));
                goto out;
        }

        op_ret = 0;

 out:
        SET_TO_OLD_FS_ID ();
        frame->root->rsp_refs = NULL;
        STACK_UNWIND (frame, op_ret, op_errno, &buf);
        return 0;
}

static int gf_posix_lk_log;

int32_t 
posix_lk (call_frame_t *frame, xlator_t *this,
          fd_t *fd, int32_t cmd, struct flock *lock)
{
        struct flock nullock = {0, };
        frame->root->rsp_refs = NULL;

        gf_posix_lk_log++;

	GF_LOG_OCCASIONALLY (gf_posix_lk_log, this->name, GF_LOG_ERROR, 
			     "\"features/posix-locks\" translator is not loaded, you need to use it");

        STACK_UNWIND (frame, -1, ENOSYS, &nullock);
        return 0;
}


int32_t 
posix_gf_file_lk (call_frame_t *frame, xlator_t *this,
		  loc_t *loc, int32_t cmd, struct flock *lock)
{
        frame->root->rsp_refs = NULL;

	gf_log (this->name, GF_LOG_CRITICAL,
		"\"features/posix-locks\" translator is not loaded. You need to use it for proper functioning of AFR");

        STACK_UNWIND (frame, -1, ENOSYS);
        return 0;
}


int32_t 
posix_gf_dir_lk (call_frame_t *frame, xlator_t *this,
		 loc_t *loc, const char *basename, gf_dir_lk_cmd cmd, 
		 gf_dir_lk_type type)
{
        frame->root->rsp_refs = NULL;

	gf_log (this->name, GF_LOG_CRITICAL,
		"\"features/posix-locks\" translator is not loaded. You need to use it for proper functioning of AFR");

        STACK_UNWIND (frame, -1, ENOSYS);
        return 0;
}


int32_t
posix_readdir (call_frame_t *frame, xlator_t *this,
               fd_t *fd, size_t size, off_t off)
{
        struct posix_fd * pfd    = NULL;
        DIR *             dir    = NULL;
        int               ret    = -1;
        size_t            filled = 0;
	int               count = 0;

        int32_t           op_ret   = -1;
        int32_t           op_errno = 0;

        gf_dirent_t *     this_entry = NULL;
	gf_dirent_t       entries;
        struct dirent *   entry      = NULL;
        off_t             in_case    = -1;
        int32_t           this_size  = -1;


        VALIDATE_OR_GOTO (frame, out);
        VALIDATE_OR_GOTO (this, out);
        VALIDATE_OR_GOTO (fd, out);

	INIT_LIST_HEAD (&entries.list);

        ret = dict_get_ptr (fd->ctx, this->name, (void **)&pfd);

        if (ret < 0) {
                gf_log (this->name, GF_LOG_ERROR,
                        "pfd is NULL, fd=%p", fd);
                op_errno = -ret;
                goto out;
        }

        dir = pfd->dir;

        if (!dir) {
                gf_log (this->name, GF_LOG_ERROR,
                        "dir is NULL for fd=%p", fd);
                op_errno = EINVAL;
                goto out;
        }


        if (!off) {
                rewinddir (dir);
        } else {
                seekdir (dir, off);
        }

        while (filled <= size) {
                in_case = telldir (dir);

                if (in_case == -1) {
                        op_errno = errno;
                        gf_log (this->name, GF_LOG_ERROR,
				"telldir failed: %s", 
                                strerror (errno));
                        goto out;
                }

                errno = 0;
                entry = readdir (dir);

                if (!entry) {
                        if (errno == EBADF) {
                                op_errno = errno;
                                gf_log (this->name, GF_LOG_ERROR,
					"readdir failed: %s", 
                                        strerror (op_errno));
                                goto out;
                        }
                        break;
                }

                this_size = dirent_size (entry);

                if (this_size + filled > size) {
                        seekdir (dir, in_case);
                        break;
                }


		this_entry = gf_dirent_for_name (entry->d_name);

		if (!this_entry) {
			gf_log (this->name, GF_LOG_ERROR,
				"could not create gf_dirent for entry %s (%s)",
				entry->d_name, strerror (errno));
			goto out;
		}
		this_entry->d_off = telldir (dir);
		this_entry->d_ino = entry->d_ino;

		list_add_tail (&this_entry->list, &entries.list);

                filled += this_size;
		count ++;
        }

        op_ret = count;

 out:
        frame->root->rsp_refs = NULL;
        STACK_UNWIND (frame, op_ret, op_errno, &entries);

	gf_dirent_free (&entries);

        return 0;
}


int32_t 
posix_stats (call_frame_t *frame, xlator_t *this,
             int32_t flags)

{
        int32_t op_ret   = -1;
        int32_t op_errno = 0;

        struct xlator_stats    xlstats = {0, };
        struct xlator_stats *  stats   = NULL;
        struct statvfs         buf     = {0,};
        struct timeval         tv      = {0,};
        struct posix_private * priv = (struct posix_private *)this->private;

        int64_t avg_read  = 0;
        int64_t avg_write = 0;
        int64_t _time_ms  = 0;
 
        DECLARE_OLD_FS_ID_VAR;

        SET_FS_ID (frame->root->uid, frame->root->gid);

        VALIDATE_OR_GOTO (frame, out);
        VALIDATE_OR_GOTO (this, out);
  
        stats = &xlstats;
    
        op_ret = statvfs (priv->base_path, &buf);
    
        if (op_ret == -1) {
                op_errno = errno;
                gf_log (this->name, GF_LOG_ERROR, "statvfs failed: %s", 
                        strerror (op_errno));
                goto out;
        }

        stats->nr_files   = priv->stats.nr_files;
        stats->nr_clients = priv->stats.nr_clients; /* client info is maintained at FSd */

        /* number of free block in the filesystem. */
        stats->free_disk  = buf.f_bfree * buf.f_bsize; 

        stats->total_disk_size = buf.f_blocks  * buf.f_bsize;
        stats->disk_usage      = (buf.f_blocks - buf.f_bavail) * buf.f_bsize;

        /* Calculate read and write usage */
        op_ret = gettimeofday (&tv, NULL);
        if (op_ret == -1) {
                op_errno = errno;
                gf_log (this->name, GF_LOG_ERROR, "gettimeofday failed: %s", strerror (errno));
                goto out;
        }
  
        /* Read */
        _time_ms  = (tv.tv_sec  - priv->init_time.tv_sec)  * 1000 +
                ((tv.tv_usec - priv->init_time.tv_usec) / 1000);

        avg_read  = (_time_ms) ? (priv->read_value  / _time_ms) : 0; /* KBps */
        avg_write = (_time_ms) ? (priv->write_value / _time_ms) : 0; /* KBps */
  
        _time_ms  = (tv.tv_sec  - priv->prev_fetch_time.tv_sec)  * 1000 +
                ((tv.tv_usec - priv->prev_fetch_time.tv_usec) / 1000);

        if (_time_ms && ((priv->interval_read  / _time_ms) > priv->max_read)) {
                priv->max_read  = (priv->interval_read / _time_ms);
        }

        if (_time_ms && ((priv->interval_write / _time_ms) > priv->max_write)) {
                priv->max_write = priv->interval_write / _time_ms;
        }

        stats->read_usage  = avg_read  / priv->max_read;
        stats->write_usage = avg_write / priv->max_write;

        op_ret = gettimeofday (&(priv->prev_fetch_time), NULL);
        if (op_ret == -1) {
                op_errno = errno;
                gf_log (this->name, GF_LOG_ERROR, "gettimeofday failed: %s", 
                        strerror (op_errno));
                goto out;
        }

        priv->interval_read  = 0;
        priv->interval_write = 0;

        op_ret = 0;

 out:
        SET_TO_OLD_FS_ID ();
  
        frame->root->rsp_refs = NULL;
        STACK_UNWIND (frame, op_ret, op_errno, stats);
        return 0;
}

int32_t 
posix_checksum (call_frame_t *frame, xlator_t *this,
                loc_t *loc, int32_t flag)
{
        char *          real_path                      = NULL;
        DIR *           dir                            = NULL;
        struct dirent * dirent                         = NULL;
        uint8_t         file_checksum[GF_FILENAME_MAX] = {0,};
        uint8_t         dir_checksum[GF_FILENAME_MAX]  = {0,};
        int32_t         op_ret                         = -1;
        int32_t         op_errno                       = 0;
        int             i                              = 0;
        int             length                         = 0;

        struct stat buf                        = {0,};
        char        tmp_real_path[GF_PATH_MAX] = {0,};
        int         ret                        = -1;

        MAKE_REAL_PATH (real_path, this, loc->path);

        dir = opendir (real_path);
  
        if (!dir){
                op_errno = errno;
                gf_log (this->name, GF_LOG_DEBUG, "opendir() failed on `%s': %s", 
                        real_path, strerror (op_errno));
                goto out;
        } 

        while ((dirent = readdir (dir))) {
                errno = 0;
                if (!dirent) {
                        if (errno != 0) {
                                op_errno = errno;
                                gf_log (this->name, GF_LOG_DEBUG, 
                                        "readdir() failed: %s", strerror (errno));
                                goto out;
                        }
                        break;
                }

                length = strlen (dirent->d_name);

                strcpy (tmp_real_path, real_path);
                strcat (tmp_real_path, "/");
                strcat (tmp_real_path, dirent->d_name);
                ret = lstat (tmp_real_path, &buf);

                if (ret == -1)
                        continue;

                if (S_ISDIR (buf.st_mode)) {
                        for (i = 0; i < length; i++)
                                dir_checksum[i] ^= dirent->d_name[i];
                } else {
                        for (i = 0; i < length; i++)
                                file_checksum[i] ^= dirent->d_name[i];
                }
        }
        closedir (dir);

        op_ret = 0;

 out:
        frame->root->rsp_refs = NULL;
        STACK_UNWIND (frame, op_ret, op_errno, file_checksum, dir_checksum);

        return 0;
}

/**
 * notify - when parent sends PARENT_UP, send CHILD_UP event from here
 */
int32_t
notify (xlator_t *this,
        int32_t event,
        void *data,
        ...)
{
        switch (event)
                {
                case GF_EVENT_PARENT_UP:
                        {
                                /* Tell the parent that posix xlator is up */
                                default_notify (this, GF_EVENT_CHILD_UP, data);
                        }
                        break;
                default:
                        /* */
                        break;
                }
        return 0;
}

/**
 * init - 
 */
int
init (xlator_t *this)
{
        int                    ret      = 0;
        int                    op_ret   = -1;
        struct stat            buf      = {0,};
        struct posix_private * _private = NULL;
        data_t *               data     = NULL;

        data = dict_get (this->options, "directory");

        _private = calloc (1, sizeof (*_private));
        if (!_private) {
                gf_log (this->name, GF_LOG_ERROR, 
                        "out of memory :(");
                ret = -1;
                goto out;
        }

        if (this->children) {
                gf_log (this->name, GF_LOG_ERROR,
                        "FATAL: storage/posix cannot have subvolumes");
                ret = -1;
                goto out;
        }

        if (!data) {
                gf_log (this->name, GF_LOG_ERROR,
                        "export directory not specified in spec file");
                ret = -1;
                goto out;
        }

        umask (000); // umask `masking' is done at the client side

        op_ret = mkdir (data->data, 0777);
        if (op_ret == 0) {
                gf_log (this->name, GF_LOG_WARNING,
                        "directory '%s' did not exist, created", data->data);
        }

        /* Check whether the specified directory exists, if not create it. */
        op_ret = stat (data->data, &buf);
        if ((ret != 0) || !S_ISDIR (buf.st_mode)) {
                gf_log (this->name, GF_LOG_ERROR, 
                        "directory '%s' doesn't exists, Exiting", data->data);
                ret = -1;
                goto out;
        }

        _private->base_path = strdup (data->data);
        _private->base_path_length = strlen (_private->base_path);

        {
                /* Stats related variables */
                gettimeofday (&_private->init_time, NULL);
                gettimeofday (&_private->prev_fetch_time, NULL);
                _private->max_read = 1;
                _private->max_write = 1;
        }

        /* Check for Extended attribute support, if not present, log it */
        op_ret = lsetxattr (data->data, "trusted.glusterfs.test", "working", 8, 0);
        if (op_ret != 0) {
                gf_log (this->name, GF_LOG_ERROR, "Extended attribute not supported");
        }

        _private->export_statfs = 1;
        data = dict_get (this->options, "export-statfs-size");
        if (data) {
                if (!strcasecmp ("no", data->data)) {
                        gf_log (this->name, GF_LOG_DEBUG, "'statfs()' returns dummy size");
                        _private->export_statfs = 0;
                }
        }

        _private->o_direct = 0;
        data = dict_get (this->options, "o-direct");
        if (data) {
                if (!strcasecmp    ("enable", data->data)
                    || !strcasecmp ("on",     data->data)) {
                        gf_log (this->name, GF_LOG_DEBUG, 
                                "o-direct mode is enabled (O_DIRECT for every open)");
                        _private->o_direct = 1;
                }
        }

#ifndef GF_DARWIN_HOST_OS
        {
                struct rlimit lim;
                lim.rlim_cur = 1048576;
                lim.rlim_max = 1048576;
    
                if (setrlimit (RLIMIT_NOFILE, &lim) == -1) {
                        gf_log (this->name, GF_LOG_WARNING, "WARNING: Failed to set 'ulimit -n 1048576': %s",
                                strerror(errno));
                        lim.rlim_cur = 65536;
                        lim.rlim_max = 65536;
        
                        if (setrlimit (RLIMIT_NOFILE, &lim) == -1) {
                                gf_log (this->name, GF_LOG_ERROR, "Failed to set max open fd to 64k: %s", strerror(errno));
                        } 
                        else {
                                gf_log (this->name, GF_LOG_ERROR, "max open fd set to 64k");
                        }
                }
        }
#endif

        this->private = (void *)_private;

 out:
        return ret;
}

void
fini (xlator_t *this)
{
        struct posix_private *priv = this->private;
        lremovexattr (priv->base_path, "trusted.glusterfs.test");
        FREE (priv);
        return;
}

struct xlator_mops mops = {
        .stats    = posix_stats,
        .lock     = mop_lock_impl,
        .unlock   = mop_unlock_impl,
};

struct xlator_fops fops = {
        .lookup      = posix_lookup,
        .forget      = posix_forget,
        .stat        = posix_stat,
        .opendir     = posix_opendir,
        .readdir     = posix_readdir,
//        .closedir    = posix_closedir,
        .readlink    = posix_readlink,
        .mknod       = posix_mknod,
        .mkdir       = posix_mkdir,
        .unlink      = posix_unlink,
        .rmelem      = posix_rmelem,
        .rmdir       = posix_rmdir,
        .symlink     = posix_symlink,
        .rename      = posix_rename,
        .link        = posix_link,
        .chmod       = posix_chmod,
        .chown       = posix_chown,
        .truncate    = posix_truncate,
        .utimens     = posix_utimens,
        .create      = posix_create,
        .open        = posix_open,
        .readv       = posix_readv,
        .writev      = posix_writev,
        .statfs      = posix_statfs,
        .flush       = posix_flush,
//        .close       = posix_close,
        .fsync       = posix_fsync,
        .incver      = posix_incver,
        .setxattr    = posix_setxattr,
        .getxattr    = posix_getxattr,
        .removexattr = posix_removexattr,
        .fsyncdir    = posix_fsyncdir,
        .access      = posix_access,
        .ftruncate   = posix_ftruncate,
        .fstat       = posix_fstat,
        .lk          = posix_lk,
	.gf_file_lk  = posix_gf_file_lk,
	.gf_dir_lk   = posix_gf_dir_lk,
        .fchown      = posix_fchown,
        .fchmod      = posix_fchmod,
        .setdents    = posix_setdents,
        .getdents    = posix_getdents,
        .checksum    = posix_checksum,
	.xattrop     = posix_xattrop,
};

struct xlator_cbks cbks = {
	.release     = posix_close,
	.releasedir  = posix_closedir
};

struct xlator_options options[] = {
	{ "o-direct", GF_OPTION_TYPE_BOOL, 0, },
	{ "directory", GF_OPTION_TYPE_PATH, 0, },
	{ "export-statfs-size", GF_OPTION_TYPE_BOOL, 0,  },
	{ NULL, 0, }
};
