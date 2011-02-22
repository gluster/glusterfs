/*
  Copyright (c) 2006-2010 Gluster, Inc. <http://www.gluster.com>
  This file is part of GlusterFS.

  GlusterFS is free software; you can redistribute it and/or modify
  it under the terms of the GNU Affero General Public License as published
  by the Free Software Foundation; either version 3 of the License,
  or (at your option) any later version.

  GlusterFS is distributed in the hope that it will be useful, but
  WITHOUT ANY WARRANTY; without even the implied warranty of
  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
  Affero General Public License for more details.

  You should have received a copy of the GNU Affero General Public License
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
#include <sys/resource.h>
#include <errno.h>
#include <libgen.h>
#include <pthread.h>
#include <ftw.h>
#include <sys/stat.h>

#ifndef GF_BSD_HOST_OS
#include <alloca.h>
#endif /* GF_BSD_HOST_OS */

#include "glusterfs.h"
#include "md5.h"
#include "checksum.h"
#include "dict.h"
#include "logging.h"
#include "posix.h"
#include "xlator.h"
#include "defaults.h"
#include "common-utils.h"
#include "compat-errno.h"
#include "compat.h"
#include "byte-order.h"
#include "syscall.h"
#include "statedump.h"
#include "locking.h"
#include "timer.h"
#include "glusterfs3-xdr.h"
#include "hashfn.h"

#define GFID_XATTR_KEY "trusted.gfid"

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

typedef struct {
  	xlator_t    *this;
  	const char  *real_path;
  	dict_t      *xattr;
  	struct iatt *stbuf;
	loc_t       *loc;
} posix_xattr_filler_t;

int
posix_forget (xlator_t *this, inode_t *inode)
{
	uint64_t tmp_cache = 0;
	if (!inode_ctx_del (inode, this, &tmp_cache))
		dict_destroy ((dict_t *)(long)tmp_cache);

	return 0;
}

static void
_posix_xattr_get_set (dict_t *xattr_req,
    		      char *key,
    		      data_t *data,
    		      void *xattrargs)
{
    	posix_xattr_filler_t *filler = xattrargs;
    	char     *value      = NULL;
    	ssize_t   xattr_size = -1;
    	int       ret      = -1;
  	char     *databuf  = NULL;
  	int       _fd      = -1;
	loc_t    *loc      = NULL;
	ssize_t  req_size  = 0;


    	/* should size be put into the data_t ? */
	if (!strcmp (key, GF_CONTENT_KEY)
            && IA_ISREG (filler->stbuf->ia_type)) {

    		/* file content request */
		req_size = data_to_uint64 (data);
		if (req_size >= filler->stbuf->ia_size) {
			_fd = open (filler->real_path, O_RDONLY);

			if (_fd == -1) {
				gf_log (filler->this->name, GF_LOG_ERROR,
					"Opening file %s failed: %s",
					filler->real_path, strerror (errno));
				goto err;
			}

			databuf = GF_CALLOC (1, filler->stbuf->ia_size,
                                             gf_posix_mt_char);

			if (!databuf) {
				gf_log (filler->this->name, GF_LOG_ERROR,
					"Out of memory.");
				goto err;
			}

			ret = read (_fd, databuf, filler->stbuf->ia_size);
			if (ret == -1) {
				gf_log (filler->this->name, GF_LOG_ERROR,
					"Read on file %s failed: %s",
					filler->real_path, strerror (errno));
				goto err;
			}

			ret = close (_fd);
			_fd = -1;
			if (ret == -1) {
				gf_log (filler->this->name, GF_LOG_ERROR,
					"Close on file %s failed: %s",
					filler->real_path, strerror (errno));
				goto err;
			}

			ret = dict_set_bin (filler->xattr, key,
					    databuf, filler->stbuf->ia_size);
			if (ret < 0) {
				goto err;
			}

			/* To avoid double free in cleanup below */
			databuf = NULL;
		err:
			if (_fd != -1)
				close (_fd);
			if (databuf)
				GF_FREE (databuf);
		}
    	} else if (!strcmp (key, GLUSTERFS_OPEN_FD_COUNT)) {
		loc = filler->loc;
		if (!list_empty (&loc->inode->fd_list)) {
			ret = dict_set_uint32 (filler->xattr, key, 1);
                        if (ret < 0)
                                gf_log (filler->this->name, GF_LOG_WARNING,
                                        "Failed to set dictionary value for %s",
                                        key);
		} else {
			ret = dict_set_uint32 (filler->xattr, key, 0);
                        if (ret < 0)
                                gf_log (filler->this->name, GF_LOG_WARNING,
                                        "Failed to set dictionary value for %s",
                                        key);
		}
	} else {
		xattr_size = sys_lgetxattr (filler->real_path, key, NULL, 0);

		if (xattr_size > 0) {
			value = GF_CALLOC (1, xattr_size + 1,
                                           gf_posix_mt_char);
                        if (!value)
                                return;

			sys_lgetxattr (filler->real_path, key, value,
                                       xattr_size);

			value[xattr_size] = '\0';
			ret = dict_set_bin (filler->xattr, key,
					    value, xattr_size);
			if (ret < 0)
				gf_log (filler->this->name, GF_LOG_DEBUG,
					"dict set failed. path: %s, key: %s",
					filler->real_path, key);
		}
	}
}


int
posix_fill_gfid_path (xlator_t *this, const char *path, struct iatt *iatt)
{
        int ret = 0;

        if (!iatt)
                return 0;

        ret = sys_lgetxattr (path, GFID_XATTR_KEY, iatt->ia_gfid, 16);
        /* Return value of getxattr */
        if (ret == 16)
                ret = 0;

        return ret;
}


int
posix_fill_gfid_fd (xlator_t *this, int fd, struct iatt *iatt)
{
        int ret = 0;

        if (!iatt)
                return 0;

        ret = sys_fgetxattr (fd, GFID_XATTR_KEY, iatt->ia_gfid, 16);
        /* Return value of getxattr */
        if (ret == 16)
                ret = 0;

        return ret;
}


int
posix_lstat_with_gfid (xlator_t *this, const char *path, struct iatt *stbuf_p)
{
        struct posix_private  *priv    = NULL;
        int                    ret     = 0;
        struct stat            lstatbuf = {0, };
        struct iatt            stbuf = {0, };

        priv = this->private;

        ret = lstat (path, &lstatbuf);
        if (ret == -1)
                return -1;

        iatt_from_stat (&stbuf, &lstatbuf);

        ret = posix_fill_gfid_path (this, path, &stbuf);
        if (ret)
                gf_log (this->name, GF_LOG_DEBUG, "failed to get gfid");

        if (stbuf_p)
                *stbuf_p = stbuf;

        return ret;
}


int
posix_fstat_with_gfid (xlator_t *this, int fd, struct iatt *stbuf_p)
{
        struct posix_private  *priv    = NULL;
        int                    ret     = 0;
        struct stat            fstatbuf = {0, };
        struct iatt            stbuf = {0, };

        priv = this->private;

        ret = fstat (fd, &fstatbuf);
        if (ret == -1)
                return -1;

        iatt_from_stat (&stbuf, &fstatbuf);

        ret = posix_fill_gfid_fd (this, fd, &stbuf);
        if (ret)
                gf_log (this->name, GF_LOG_DEBUG, "failed to set gfid");

        if (stbuf_p)
                *stbuf_p = stbuf;

        return ret;
}


dict_t *
posix_lookup_xattr_fill (xlator_t *this, const char *real_path, loc_t *loc,
    			 dict_t *xattr_req, struct iatt *buf)
{
    	dict_t     *xattr             = NULL;
    	posix_xattr_filler_t filler   = {0, };

    	xattr = get_new_dict();
    	if (!xattr) {
    		gf_log (this->name, GF_LOG_ERROR,
    			"Out of memory.");
    		goto out;
    	}

    	filler.this      = this;
    	filler.real_path = real_path;
    	filler.xattr     = xattr;
    	filler.stbuf     = buf;
	filler.loc       = loc;

    	dict_foreach (xattr_req, _posix_xattr_get_set, &filler);
out:
    	return xattr;
}


/*
 * If the parent directory of {real_path} has the setgid bit set,
 * then set {gid} to the gid of the parent. Otherwise,
 * leave {gid} unchanged.
 */

int
setgid_override (xlator_t *this, char *real_path, gid_t *gid)
{
        char *                 tmp_path     = NULL;
        char *                 parent_path  = NULL;
        struct iatt            parent_stbuf;

        int op_ret = 0;

        tmp_path = gf_strdup (real_path);
        if (!tmp_path) {
                op_ret = -ENOMEM;
                gf_log ("[storage/posix]", GF_LOG_ERROR,
                        "Out of memory");
                goto out;
        }

        parent_path = dirname (tmp_path);

        op_ret = posix_lstat_with_gfid (this, parent_path, &parent_stbuf);

        if (op_ret == -1) {
                op_ret = -errno;
                gf_log ("[storage/posix]", GF_LOG_ERROR,
                        "lstat on parent directory (%s) failed: %s",
                        parent_path, strerror (errno));
                goto out;
        }

        if (parent_stbuf.ia_prot.sgid) {
                /*
                   Entries created inside a setgid directory
                   should inherit the gid from the parent
                */

                *gid = parent_stbuf.ia_gid;
        }
out:

        if (tmp_path)
                GF_FREE (tmp_path);

        return op_ret;
}


int
posix_gfid_set (xlator_t *this, const char *path, dict_t *xattr_req)
{
        void        *uuid_req = NULL;
        uuid_t       uuid_curr;
        int          ret = 0;
        struct stat  stat = {0, };

        if (!xattr_req)
                return 0;

        if (sys_lstat (path, &stat) != 0)
                return 0;

        ret = sys_lgetxattr (path, GFID_XATTR_KEY, uuid_curr, 16);

        if (ret == 16)
                return 0;

        ret = dict_get_ptr (xattr_req, "gfid-req", &uuid_req);
        if (ret)
                goto out;

        ret = sys_lsetxattr (path, GFID_XATTR_KEY, uuid_req, 16, XATTR_CREATE);

out:
        return ret;
}


int32_t
posix_lookup (call_frame_t *frame, xlator_t *this,
              loc_t *loc, dict_t *xattr_req)
{
        struct iatt buf                = {0, };
        char *      real_path          = NULL;
        int32_t     op_ret             = -1;
        int32_t     entry_ret          = 0;
        int32_t     op_errno           = 0;
        dict_t *    xattr              = NULL;
        char *      pathdup            = NULL;
        char *      parentpath         = NULL;
        struct iatt postparent         = {0,};

        VALIDATE_OR_GOTO (frame, out);
        VALIDATE_OR_GOTO (this, out);
        VALIDATE_OR_GOTO (loc, out);
	VALIDATE_OR_GOTO (loc->path, out);

        MAKE_REAL_PATH (real_path, this, loc->path);

        posix_gfid_set (this, real_path, xattr_req);

        op_ret   = posix_lstat_with_gfid (this, real_path, &buf);
        op_errno = errno;

        if (op_ret == -1) {
		if (op_errno != ENOENT) {
			gf_log (this->name, GF_LOG_ERROR,
				"lstat on %s failed: %s",
				loc->path, strerror (op_errno));
		}

                entry_ret = -1;
                goto parent;
        }

        if (xattr_req && (op_ret == 0)) {
		xattr = posix_lookup_xattr_fill (this, real_path, loc,
						 xattr_req, &buf);
        }

parent:
        if (loc->parent) {
                pathdup = gf_strdup (real_path);
                GF_VALIDATE_OR_GOTO (this->name, pathdup, out);

                parentpath = dirname (pathdup);

                op_ret = posix_lstat_with_gfid (this, parentpath, &postparent);
                if (op_ret == -1) {
                        op_errno = errno;
                        gf_log (this->name, GF_LOG_ERROR,
                                "post-operation lstat on parent of %s failed: %s",
                                loc->path, strerror (op_errno));
                        goto out;
                }
        }

        op_ret = entry_ret;
out:
        if (pathdup)
                GF_FREE (pathdup);

        if (xattr)
                dict_ref (xattr);

        STACK_UNWIND_STRICT (lookup, frame, op_ret, op_errno,
                             (loc)?loc->inode:NULL, &buf, xattr, &postparent);

        if (xattr)
                dict_unref (xattr);

        return 0;
}


int32_t
posix_stat (call_frame_t *frame,
            xlator_t *this,
            loc_t *loc)
{
        struct iatt           buf       = {0,};
        char *                real_path = NULL;
        int32_t               op_ret    = -1;
        int32_t               op_errno  = 0;
        struct posix_private *priv      = NULL; 

        DECLARE_OLD_FS_ID_VAR;

        VALIDATE_OR_GOTO (frame, out);
        VALIDATE_OR_GOTO (this, out);
        VALIDATE_OR_GOTO (loc, out);

        priv = this->private;
        VALIDATE_OR_GOTO (priv, out);

        SET_FS_ID (frame->root->uid, frame->root->gid);
        MAKE_REAL_PATH (real_path, this, loc->path);

        op_ret = posix_lstat_with_gfid (this, real_path, &buf);
        if (op_ret == -1) {
                op_errno = errno;
                gf_log (this->name, GF_LOG_ERROR,
                        "lstat on %s failed: %s", loc->path, 
                        strerror (op_errno));
                goto out;
        }

        op_ret = 0;

 out:
        SET_TO_OLD_FS_ID();
        STACK_UNWIND_STRICT (stat, frame, op_ret, op_errno, &buf);

        return 0;
}

static int
posix_do_chmod (xlator_t *this, const char *path, struct iatt *stbuf)
{
        int32_t     ret = -1;
        mode_t      mode = 0;
        struct stat stat;
        int         is_symlink = 0;

        ret = sys_lstat (path, &stat);
        if (ret != 0) {
                gf_log (this->name, GF_LOG_WARNING,
                        "%s (%s)", path, strerror (errno));
                goto out;
        }

        if (S_ISLNK (stat.st_mode))
                is_symlink = 1;

        mode = st_mode_from_ia (stbuf->ia_prot, stbuf->ia_type);
        ret = lchmod (path, mode);
        if ((ret == -1) && (errno == ENOSYS)) {
                /* in Linux symlinks are always in mode 0777 and no
                   such call as lchmod exists.
                */
                if (is_symlink) {
                        ret = 0;
                        goto out;
                }

                ret = chmod (path, mode);
        }
out:
        return ret;
}

static int
posix_do_chown (xlator_t *this,
                const char *path,
                struct iatt *stbuf,
                int32_t valid)
{
        int32_t ret = -1;
        uid_t uid = -1;
        gid_t gid = -1;

        if (valid & GF_SET_ATTR_UID)
                uid = stbuf->ia_uid;

        if (valid & GF_SET_ATTR_GID)
                gid = stbuf->ia_gid;

        ret = lchown (path, uid, gid);

        return ret;
}

static int
posix_do_utimes (xlator_t *this,
                 const char *path,
                 struct iatt *stbuf)
{
        int32_t ret = -1;
        struct timeval tv[2]     = {{0,},{0,}};
        struct stat stat;
        int    is_symlink = 0;

        ret = sys_lstat (path, &stat);
        if (ret != 0) {
                gf_log (this->name, GF_LOG_WARNING,
                        "%s (%s)", path, strerror (errno));
                goto out;
        }

        if (S_ISLNK (stat.st_mode))
                is_symlink = 1;

        tv[0].tv_sec  = stbuf->ia_atime;
        tv[0].tv_usec = stbuf->ia_atime_nsec / 1000;
        tv[1].tv_sec  = stbuf->ia_mtime;
        tv[1].tv_usec = stbuf->ia_mtime_nsec / 1000;

        ret = lutimes (path, tv);
        if ((ret == -1) && (errno == ENOSYS)) {
                if (is_symlink) {
                        ret = 0;
                        goto out;
                }

                ret = utimes (path, tv);
	}

out:
        return ret;
}

int
posix_setattr (call_frame_t *frame, xlator_t *this,
               loc_t *loc, struct iatt *stbuf, int32_t valid)
{
        int32_t        op_ret    = -1;
        int32_t        op_errno  = 0;
        char *         real_path = 0;
        struct iatt    statpre     = {0,};
        struct iatt    statpost    = {0,};

        DECLARE_OLD_FS_ID_VAR;

        VALIDATE_OR_GOTO (frame, out);
        VALIDATE_OR_GOTO (this, out);
        VALIDATE_OR_GOTO (loc, out);

        SET_FS_ID (frame->root->uid, frame->root->gid);
        MAKE_REAL_PATH (real_path, this, loc->path);

        op_ret = posix_lstat_with_gfid (this, real_path, &statpre);
        if (op_ret == -1) {
                op_errno = errno;
                gf_log (this->name, GF_LOG_ERROR,
                        "setattr (lstat) on %s failed: %s", real_path,
                        strerror (op_errno));
                goto out;
        }

        if (valid & GF_SET_ATTR_MODE) {
                op_ret = posix_do_chmod (this, real_path, stbuf);
                if (op_ret == -1) {
                        op_errno = errno;
                        gf_log (this->name, GF_LOG_ERROR,
                                "setattr (chmod) on %s failed: %s", real_path,
                                strerror (op_errno));
                        goto out;
                }
        }

        if (valid & (GF_SET_ATTR_UID | GF_SET_ATTR_GID)){
                op_ret = posix_do_chown (this, real_path, stbuf, valid);
                if (op_ret == -1) {
                        op_errno = errno;
                        gf_log (this->name, GF_LOG_ERROR,
                                "setattr (chown) on %s failed: %s", real_path,
                                strerror (op_errno));
                        goto out;
                }
        }

        if (valid & (GF_SET_ATTR_ATIME | GF_SET_ATTR_MTIME)) {
                op_ret = posix_do_utimes (this, real_path, stbuf);
                if (op_ret == -1) {
                        op_errno = errno;
                        gf_log (this->name, GF_LOG_ERROR,
                                "setattr (utimes) on %s failed: %s", real_path,
                                strerror (op_errno));
                        goto out;
                }
        }

        if (!valid) {
                op_ret = lchown (real_path, -1, -1);
                if (op_ret == -1) {
                        op_errno = errno;
                        gf_log (this->name, GF_LOG_ERROR,
                                "lchown (%s, -1, -1) failed => (%s)",
                                real_path, strerror (op_errno));

                        goto out;
                }
        }

        op_ret = posix_lstat_with_gfid (this, real_path, &statpost);
        if (op_ret == -1) {
                op_errno = errno;
                gf_log (this->name, GF_LOG_ERROR,
                        "setattr (lstat) on %s failed: %s", real_path,
                        strerror (op_errno));
                goto out;
        }

        op_ret = 0;

out:
        SET_TO_OLD_FS_ID ();

        STACK_UNWIND_STRICT (setattr, frame, op_ret, op_errno,
                             &statpre, &statpost);

        return 0;
}

int32_t
posix_do_fchown (xlator_t *this,
                 int fd,
                 struct iatt *stbuf,
                 int32_t valid)
{
        int   ret      = -1;
        uid_t uid = -1;
        gid_t gid = -1;

        if (valid & GF_SET_ATTR_UID)
                uid = stbuf->ia_uid;

        if (valid & GF_SET_ATTR_GID)
                gid = stbuf->ia_gid;

        ret = fchown (fd, uid, gid);

        return ret;
}


int32_t
posix_do_fchmod (xlator_t *this,
                 int fd, struct iatt *stbuf)
{
        mode_t  mode = 0;

        mode = st_mode_from_ia (stbuf->ia_prot, stbuf->ia_type);
        return fchmod (fd, mode);
}

static int
posix_do_futimes (xlator_t *this,
                  int fd,
                  struct iatt *stbuf)
{
        errno = ENOSYS;
        return -1;
}

int
posix_fsetattr (call_frame_t *frame, xlator_t *this,
                fd_t *fd, struct iatt *stbuf, int32_t valid)
{
        int32_t        op_ret    = -1;
        int32_t        op_errno  = 0;
        struct iatt    statpre     = {0,};
        struct iatt    statpost    = {0,};
        struct posix_fd *pfd = NULL;
        uint64_t         tmp_pfd = 0;
        int32_t          ret = -1;

        DECLARE_OLD_FS_ID_VAR;

        SET_FS_ID (frame->root->uid, frame->root->gid);

        VALIDATE_OR_GOTO (frame, out);
        VALIDATE_OR_GOTO (this, out);
        VALIDATE_OR_GOTO (fd, out);

        ret = fd_ctx_get (fd, this, &tmp_pfd);
        if (ret < 0) {
                op_errno = -ret;
                gf_log (this->name, GF_LOG_DEBUG,
			"pfd is NULL from fd=%p", fd);
                goto out;
        }
	pfd = (struct posix_fd *)(long)tmp_pfd;

        op_ret = posix_fstat_with_gfid (this, pfd->fd, &statpre);
        if (op_ret == -1) {
                op_errno = errno;
                gf_log (this->name, GF_LOG_ERROR,
                        "fsetattr (fstat) failed on fd=%p: %s", fd,
                        strerror (op_errno));
                goto out;
        }

        if (valid & GF_SET_ATTR_MODE) {
                op_ret = posix_do_fchmod (this, pfd->fd, stbuf);
                if (op_ret == -1) {
                        op_errno = errno;
                        gf_log (this->name, GF_LOG_ERROR,
                                "fsetattr (fchmod) failed on fd=%p: %s",
                                fd, strerror (op_errno));
                        goto out;
                }
        }

        if (valid & (GF_SET_ATTR_UID | GF_SET_ATTR_GID)) {
                op_ret = posix_do_fchown (this, pfd->fd, stbuf, valid);
                if (op_ret == -1) {
                        op_errno = errno;
                        gf_log (this->name, GF_LOG_ERROR,
                                "fsetattr (fchown) failed on fd=%p: %s",
                                fd, strerror (op_errno));
                        goto out;
                }

        }

        if (valid & (GF_SET_ATTR_ATIME | GF_SET_ATTR_MTIME)) {
                op_ret = posix_do_futimes (this, pfd->fd, stbuf);
                if (op_ret == -1) {
                        op_errno = errno;
                        gf_log (this->name, GF_LOG_ERROR,
                                "fsetattr (futimes) on failed fd=%p: %s", fd,
                                strerror (op_errno));
                        goto out;
                }
        }

        if (!valid) {
                op_ret = fchown (pfd->fd, -1, -1);
                if (op_ret == -1) {
                        op_errno = errno;
                        gf_log (this->name, GF_LOG_ERROR,
                                "fchown (%d, -1, -1) failed => (%s)",
                                pfd->fd, strerror (op_errno));

                        goto out;
                }
        }

        op_ret = posix_fstat_with_gfid (this, pfd->fd, &statpost);
        if (op_ret == -1) {
                op_errno = errno;
                gf_log (this->name, GF_LOG_ERROR,
                        "fsetattr (fstat) failed on fd=%p: %s", fd,
                        strerror (op_errno));
                goto out;
        }

        op_ret = 0;

out:
        SET_TO_OLD_FS_ID ();

        STACK_UNWIND_STRICT (fsetattr, frame, op_ret, op_errno,
                             &statpre, &statpost);

        return 0;
}

int32_t
posix_opendir (call_frame_t *frame, xlator_t *this,
               loc_t *loc, fd_t *fd)
{
        char *            real_path = NULL;
        int32_t           op_ret    = -1;
        int32_t           op_errno  = EINVAL;
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
                        "opendir failed on %s: %s",
			loc->path, strerror (op_errno));
                goto out;
        }

        op_ret = dirfd (dir);
	if (op_ret < 0) {
                op_errno = errno;
                gf_log (this->name, GF_LOG_ERROR,
                        "dirfd() failed on %s: %s",
			loc->path, strerror (op_errno));
		goto out;
	}

        pfd = GF_CALLOC (1, sizeof (*pfd), gf_posix_mt_posix_fd);
        if (!pfd) {
                op_errno = errno;
                gf_log (this->name, GF_LOG_ERROR,
                        "Out of memory.");
                goto out;
        }

        pfd->dir = dir;
        pfd->fd = dirfd (dir);
        pfd->path = gf_strdup (real_path);
        if (!pfd->path) {
                gf_log (this->name, GF_LOG_ERROR,
                        "Out of memory.");
                goto out;
        }

	fd_ctx_set (fd, this, (uint64_t)(long)pfd);

        op_ret = 0;

 out:
        if (op_ret == -1) {
                if (dir) {
                        closedir (dir);
                        dir = NULL;
                }
                if (pfd) {
                        if (pfd->path)
                                GF_FREE (pfd->path);
                        GF_FREE (pfd);
                        pfd = NULL;
                }
        }

        SET_TO_OLD_FS_ID ();
        STACK_UNWIND_STRICT (opendir, frame, op_ret, op_errno, fd);
        return 0;
}

int32_t
posix_releasedir (xlator_t *this,
		  fd_t *fd)
{
        struct posix_fd * pfd      = NULL;
	uint64_t          tmp_pfd  = 0;
        int               ret      = 0;

        struct posix_private *priv = NULL;

        VALIDATE_OR_GOTO (this, out);
        VALIDATE_OR_GOTO (fd, out);

        ret = fd_ctx_del (fd, this, &tmp_pfd);
        if (ret < 0) {
                gf_log (this->name, GF_LOG_DEBUG,
                        "pfd from fd=%p is NULL", fd);
                goto out;
        }

	pfd = (struct posix_fd *)(long)tmp_pfd;
        if (!pfd->dir) {
                gf_log (this->name, GF_LOG_DEBUG,
                        "pfd->dir is NULL for fd=%p path=%s",
                        fd, pfd->path ? pfd->path : "<NULL>");
                goto out;
        }

        priv = this->private;

        if (!pfd->path) {
                gf_log (this->name, GF_LOG_DEBUG,
                        "pfd->path was NULL. fd=%p pfd=%p",
                        fd, pfd);
        }

        pthread_mutex_lock (&priv->janitor_lock);
        {
                INIT_LIST_HEAD (&pfd->list);
                list_add_tail (&pfd->list, &priv->janitor_fds);
                pthread_cond_signal (&priv->janitor_cond);
        }
        pthread_mutex_unlock (&priv->janitor_lock);

 out:
        return 0;
}


int32_t
posix_readlink (call_frame_t *frame, xlator_t *this,
                loc_t *loc, size_t size)
{
        char *  dest      = NULL;
        int32_t op_ret    = -1;
        int32_t lstat_ret = -1;
        int32_t op_errno  = 0;
        char *  real_path = NULL;
        struct iatt stbuf = {0,};

        DECLARE_OLD_FS_ID_VAR;

	VALIDATE_OR_GOTO (frame, out);

        SET_FS_ID (frame->root->uid, frame->root->gid);

        dest = alloca (size + 1);

        MAKE_REAL_PATH (real_path, this, loc->path);

        op_ret = readlink (real_path, dest, size);
        if (op_ret == -1) {
                op_errno = errno;
                gf_log (this->name, GF_LOG_ERROR,
                        "readlink on %s failed: %s", loc->path, 
                        strerror (op_errno));
                goto out;
        }

        dest[op_ret] = 0;

        lstat_ret = posix_lstat_with_gfid (this, real_path, &stbuf);
        if (lstat_ret == -1) {
                op_ret = -1;
                op_errno = errno;
                gf_log (this->name, GF_LOG_ERROR,
                        "lstat on %s failed: %s", loc->path,
                        strerror (op_errno));
                goto out;
        }

 out:
        SET_TO_OLD_FS_ID ();

        STACK_UNWIND_STRICT (readlink, frame, op_ret, op_errno, dest, &stbuf);

        return 0;
}


int
posix_mknod (call_frame_t *frame, xlator_t *this,
             loc_t *loc, mode_t mode, dev_t dev, dict_t *params)
{
	int                   tmp_fd      = 0;
        int32_t               op_ret      = -1;
        int32_t               op_errno    = 0;
        char                 *real_path   = 0;
        struct iatt           stbuf       = { 0, };
        char                  was_present = 1;
        struct posix_private *priv        = NULL;
        gid_t                 gid         = 0;
        char                 *pathdup   = NULL;
        struct iatt           preparent = {0,};
        struct iatt           postparent = {0,};
        char                 *parentpath = NULL;

        DECLARE_OLD_FS_ID_VAR;

        VALIDATE_OR_GOTO (frame, out);
        VALIDATE_OR_GOTO (this, out);
        VALIDATE_OR_GOTO (loc, out);

        priv = this->private;
        VALIDATE_OR_GOTO (priv, out);

        MAKE_REAL_PATH (real_path, this, loc->path);

        gid = frame->root->gid;

        op_ret = setgid_override (this, real_path, &gid);
        if (op_ret < 0) {
                op_errno = -op_ret;
                op_ret = -1;
                goto out;
        }

        SET_FS_ID (frame->root->uid, gid);
        pathdup = gf_strdup (real_path);
        GF_VALIDATE_OR_GOTO (this->name, pathdup, out);

        parentpath = dirname (pathdup);

        op_ret = posix_lstat_with_gfid (this, parentpath, &preparent);
        if (op_ret == -1) {
                op_errno = errno;
                gf_log (this->name, GF_LOG_ERROR,
                        "pre-operation lstat on parent of %s failed: %s",
                        loc->path, strerror (op_errno));
                goto out;
        }

        op_ret = mknod (real_path, mode, dev);

        if (op_ret == -1) {
                op_errno = errno;
		if ((op_errno == EINVAL) && S_ISREG (mode)) {
			/* Over Darwin, mknod with (S_IFREG|mode)
			   doesn't work */
			tmp_fd = creat (real_path, mode);
			if (tmp_fd == -1)
				goto out;
			close (tmp_fd);
		} else {

			gf_log (this->name, GF_LOG_ERROR,
				"mknod on %s failed: %s", loc->path,
				strerror (op_errno));
			goto out;
		}
        }

        op_ret = posix_gfid_set (this, real_path, params);

#ifndef HAVE_SET_FSID
        op_ret = lchown (real_path, frame->root->uid, gid);
        if (op_ret == -1) {
                op_errno = errno;
                gf_log (this->name, GF_LOG_ERROR,
                        "lchown on %s failed: %s", loc->path, 
                        strerror (op_errno));
                goto out;
        }
#endif

        op_ret = posix_lstat_with_gfid (this, real_path, &stbuf);
        if (op_ret == -1) {
                op_errno = errno;
                gf_log (this->name, GF_LOG_ERROR,
                        "mknod on %s failed: %s", loc->path, 
                        strerror (op_errno));
                goto out;
        }

        op_ret = posix_lstat_with_gfid (this, parentpath, &postparent);
        if (op_ret == -1) {
                op_errno = errno;
                gf_log (this->name, GF_LOG_ERROR,
                        "post-operation lstat on parent of %s failed: %s",
                        loc->path, strerror (op_errno));
                goto out;
        }

        op_ret = 0;

 out:
        if (pathdup)
                GF_FREE (pathdup);

        SET_TO_OLD_FS_ID ();

        STACK_UNWIND_STRICT (mknod, frame, op_ret, op_errno,
                             (loc)?loc->inode:NULL, &stbuf, &preparent, &postparent);

        if ((op_ret == -1) && (!was_present)) {
                unlink (real_path);
        }

        return 0;
}


static int
janitor_walker (const char *fpath, const struct stat *sb,
                int typeflag, struct FTW *ftwbuf)
{
        switch (sb->st_mode & S_IFMT) {
        case S_IFREG:
        case S_IFBLK:
        case S_IFLNK:
        case S_IFCHR:
        case S_IFIFO:
        case S_IFSOCK:
                gf_log (THIS->name, GF_LOG_TRACE,
                        "unlinking %s", fpath);
                unlink (fpath);
                break;

        case S_IFDIR:
                if (ftwbuf->level) { /* don't remove top level dir */
                        gf_log (THIS->name, GF_LOG_TRACE,
                                "removing directory %s", fpath);

                        rmdir (fpath);
                }
                break;
        }

        return 0;   /* 0 = FTW_CONTINUE */
}


static struct posix_fd *
janitor_get_next_fd (xlator_t *this)
{
        struct posix_private *priv = NULL;
        struct posix_fd *pfd = NULL;

        struct timespec timeout;

        priv = this->private;

        pthread_mutex_lock (&priv->janitor_lock);
        {
                if (list_empty (&priv->janitor_fds)) {
                        time (&timeout.tv_sec);
                        timeout.tv_sec += priv->janitor_sleep_duration;
                        timeout.tv_nsec = 0;

                        pthread_cond_timedwait (&priv->janitor_cond,
                                                &priv->janitor_lock,
                                                &timeout);
                        goto unlock;
                }

                pfd = list_entry (priv->janitor_fds.next, struct posix_fd,
                                  list);

                list_del (priv->janitor_fds.next);
        }
unlock:
        pthread_mutex_unlock (&priv->janitor_lock);

        return pfd;
}


static void *
posix_janitor_thread_proc (void *data)
{
        xlator_t *            this = NULL;
        struct posix_private *priv = NULL;
        struct posix_fd *pfd;

        time_t now;

        this = data;
        priv = this->private;

        THIS = this;

        while (1) {
                time (&now);
                if ((now - priv->last_landfill_check) > priv->janitor_sleep_duration) {
                        gf_log (this->name, GF_LOG_TRACE,
                                "janitor cleaning out /" GF_REPLICATE_TRASH_DIR);

                        nftw (priv->trash_path,
                              janitor_walker,
                              32,
                              FTW_DEPTH | FTW_PHYS);

                        priv->last_landfill_check = now;
                }

                pfd = janitor_get_next_fd (this);
                if (pfd) {
                        if (pfd->dir == NULL) {
                                gf_log (this->name, GF_LOG_TRACE,
                                        "janitor: closing file fd=%d", pfd->fd);
                                close (pfd->fd);
                        } else {
                                gf_log (this->name, GF_LOG_TRACE,
                                        "janitor: closing dir fd=%p", pfd->dir);
                                closedir (pfd->dir);
                        }

                        if (pfd->path)
                                GF_FREE (pfd->path);

                        GF_FREE (pfd);
                }
        }

        return NULL;
}


static void
posix_spawn_janitor_thread (xlator_t *this)
{
        struct posix_private *priv = NULL;
        int ret = 0;

        priv = this->private;

        LOCK (&priv->lock);
        {
                if (!priv->janitor_present) {
                        ret = pthread_create (&priv->janitor, NULL,
                                              posix_janitor_thread_proc, this);

                        if (ret < 0) {
                                gf_log (this->name, GF_LOG_ERROR,
                                        "spawning janitor thread failed: %s",
                                        strerror (errno));
                                goto unlock;
                        }

                        priv->janitor_present = _gf_true;
                }
        }
unlock:
        UNLOCK (&priv->lock);
}


int
posix_mkdir (call_frame_t *frame, xlator_t *this,
             loc_t *loc, mode_t mode, dict_t *params)
{
        int32_t               op_ret      = -1;
        int32_t               op_errno    = 0;
        char                 *real_path   = NULL;
        struct iatt           stbuf       = {0, };
        char                  was_present = 1;
        struct posix_private *priv        = NULL;
        gid_t                 gid         = 0;
        char                 *pathdup   = NULL;
        char                 *parentpath = NULL;
        struct iatt           preparent = {0,};
        struct iatt           postparent = {0,};

        DECLARE_OLD_FS_ID_VAR;

        VALIDATE_OR_GOTO (frame, out);
        VALIDATE_OR_GOTO (this, out);
        VALIDATE_OR_GOTO (loc, out);

        priv = this->private;
        VALIDATE_OR_GOTO (priv, out);

        MAKE_REAL_PATH (real_path, this, loc->path);

        gid = frame->root->gid;

        op_ret = posix_lstat_with_gfid (this, real_path, &stbuf);
        if ((op_ret == -1) && (errno == ENOENT)) {
                was_present = 0;
        }

        op_ret = setgid_override (this, real_path, &gid);
        if (op_ret < 0) {
                op_errno = -op_ret;
                op_ret = -1;
                goto out;
        }

        SET_FS_ID (frame->root->uid, gid);
        pathdup = gf_strdup (real_path);
        GF_VALIDATE_OR_GOTO (this->name, pathdup, out);

        parentpath = dirname (pathdup);

        op_ret = posix_lstat_with_gfid (this, parentpath, &preparent);
        if (op_ret == -1) {
                op_errno = errno;
                gf_log (this->name, GF_LOG_ERROR,
                        "pre-operation lstat on parent of %s failed: %s",
                        loc->path, strerror (op_errno));
                goto out;
        }

        op_ret = mkdir (real_path, mode);
        if (op_ret == -1) {
                op_errno = errno;
                gf_log (this->name, GF_LOG_ERROR,
                        "mkdir of %s failed: %s", loc->path, 
                        strerror (op_errno));
                goto out;
        }

        op_ret = posix_gfid_set (this, real_path, params);

#ifndef HAVE_SET_FSID
        op_ret = chown (real_path, frame->root->uid, gid);
        if (op_ret == -1) {
                op_errno = errno;
                gf_log (this->name, GF_LOG_ERROR,
                        "chown on %s failed: %s", loc->path, 
                        strerror (op_errno));
                goto out;
        }
#endif

        op_ret = posix_lstat_with_gfid (this, real_path, &stbuf);
        if (op_ret == -1) {
                op_errno = errno;
                gf_log (this->name, GF_LOG_ERROR,
                        "lstat on %s failed: %s", loc->path, 
                        strerror (op_errno));
                goto out;
        }

        op_ret = posix_lstat_with_gfid (this, parentpath, &postparent);
        if (op_ret == -1) {
                op_errno = errno;
                gf_log (this->name, GF_LOG_ERROR,
                        "post-operation lstat on parent of %s failed: %s",
                        loc->path, strerror (op_errno));
                goto out;
        }

        op_ret = 0;

 out:
        if (pathdup)
                GF_FREE (pathdup);

        SET_TO_OLD_FS_ID ();

        STACK_UNWIND_STRICT (mkdir, frame, op_ret, op_errno,
                             (loc)?loc->inode:NULL, &stbuf, &preparent, &postparent);

        if ((op_ret == -1) && (!was_present)) {
                unlink (real_path);
        }

        return 0;
}


int32_t
posix_unlink (call_frame_t *frame, xlator_t *this,
              loc_t *loc)
{
        int32_t                  op_ret    = -1;
        int32_t                  op_errno  = 0;
        char                    *real_path = NULL;
        char                    *pathdup   = NULL;
        char                    *parentpath = NULL;
        int32_t                  fd = -1;
        struct posix_private    *priv      = NULL;
        struct iatt            preparent = {0,};
        struct iatt            postparent = {0,};

        DECLARE_OLD_FS_ID_VAR;

        VALIDATE_OR_GOTO (frame, out);
        VALIDATE_OR_GOTO (this, out);
        VALIDATE_OR_GOTO (loc, out);

        SET_FS_ID (frame->root->uid, frame->root->gid);
        MAKE_REAL_PATH (real_path, this, loc->path);

        pathdup = gf_strdup (real_path);
        GF_VALIDATE_OR_GOTO (this->name, pathdup, out);

        parentpath = dirname (pathdup);

        op_ret = posix_lstat_with_gfid (this, parentpath, &preparent);
        if (op_ret == -1) {
                op_errno = errno;
                gf_log (this->name, GF_LOG_ERROR,
                        "pre-operation lstat on parent of %s failed: %s",
                        loc->path, strerror (op_errno));
                goto out;
        }

        priv = this->private;
        if (priv->background_unlink) {
                if (IA_ISREG (loc->inode->ia_type)) {
                        fd = open (real_path, O_RDONLY);
                        if (fd == -1) {
                                op_ret = -1;
                                op_errno = errno;
                                gf_log (this->name, GF_LOG_ERROR,
                                        "open of %s failed: %s", loc->path,
                                        strerror (op_errno));
                                goto out;
                        }
                }
        }

        op_ret = unlink (real_path);
        if (op_ret == -1) {
                op_errno = errno;
                gf_log (this->name, GF_LOG_ERROR,
                        "unlink of %s failed: %s", loc->path, 
                        strerror (op_errno));
                goto out;
        }

        op_ret = posix_lstat_with_gfid (this, parentpath, &postparent);
        if (op_ret == -1) {
                op_errno = errno;
                gf_log (this->name, GF_LOG_ERROR,
                        "post-operation lstat on parent of %s failed: %s",
                        loc->path, strerror (op_errno));
                goto out;
        }

        op_ret = 0;

 out:
        if (pathdup)
                GF_FREE (pathdup);

        SET_TO_OLD_FS_ID ();

        STACK_UNWIND_STRICT (unlink, frame, op_ret, op_errno,
                             &preparent, &postparent);

        if (fd != -1) {
                close (fd);
        }

        return 0;
}


int
posix_rmdir (call_frame_t *frame, xlator_t *this,
             loc_t *loc, int flags)
{
        int32_t op_ret    = -1;
        int32_t op_errno  = 0;
        char *  real_path = NULL;
        char *  pathdup   = NULL;
        char *  parentpath = NULL;
        struct iatt   preparent = {0,};
        struct iatt   postparent = {0,};
        struct posix_private    *priv      = NULL;

        DECLARE_OLD_FS_ID_VAR;

        VALIDATE_OR_GOTO (frame, out);
        VALIDATE_OR_GOTO (this, out);
        VALIDATE_OR_GOTO (loc, out);

        priv = this->private;

        SET_FS_ID (frame->root->uid, frame->root->gid);
        MAKE_REAL_PATH (real_path, this, loc->path);

        pathdup = gf_strdup (real_path);
        GF_VALIDATE_OR_GOTO (this->name, pathdup, out);

        parentpath = dirname (pathdup);

        op_ret = posix_lstat_with_gfid (this, parentpath, &preparent);
        if (op_ret == -1) {
                op_errno = errno;
                gf_log (this->name, GF_LOG_ERROR,
                        "pre-operation lstat on parent of %s failed: %s",
                        loc->path, strerror (op_errno));
                goto out;
        }

        if (flags) {
                uint32_t hashval = 0;
                char *tmp_path = alloca (strlen (priv->trash_path) + 16);

                mkdir (priv->trash_path, 0755);
                hashval = gf_dm_hashfn (real_path, strlen (real_path));
                sprintf (tmp_path, "%s/%u", priv->trash_path, hashval);
                op_ret = rename (real_path, tmp_path);
        } else {
                op_ret = rmdir (real_path);
        }
        op_errno = errno;

	if (op_errno == EEXIST)
		/* Solaris sets errno = EEXIST instead of ENOTEMPTY */
		op_errno = ENOTEMPTY;

        /* No need to log a common error as ENOTEMPTY */
        if (op_ret == -1 && op_errno != ENOTEMPTY) {
                gf_log (this->name, GF_LOG_ERROR,
                        "rmdir of %s failed: %s", loc->path, 
                        strerror (op_errno));
        }

        if (op_ret == -1)
                goto out;

        op_ret = posix_lstat_with_gfid (this, parentpath, &postparent);
        if (op_ret == -1) {
                op_errno = errno;
                gf_log (this->name, GF_LOG_ERROR,
                        "post-operation lstat on parent of %s failed: %s",
                        loc->path, strerror (op_errno));
                goto out;
        }

 out:
        if (pathdup)
                GF_FREE (pathdup);

        SET_TO_OLD_FS_ID ();

        STACK_UNWIND_STRICT (rmdir, frame, op_ret, op_errno,
                             &preparent, &postparent);

        return 0;
}


int
posix_symlink (call_frame_t *frame, xlator_t *this,
               const char *linkname, loc_t *loc, dict_t *params)
{
        int32_t               op_ret      = -1;
        int32_t               op_errno    = 0;
        char *                real_path   = 0;
        struct iatt           stbuf       = { 0, };
        struct posix_private *priv        = NULL;
        gid_t                 gid         = 0;
        char                  was_present = 1; 
        char                 *pathdup   = NULL;
        char                 *parentpath = NULL;
        struct iatt           preparent = {0,};
        struct iatt           postparent = {0,};

        DECLARE_OLD_FS_ID_VAR;

        VALIDATE_OR_GOTO (frame, out);
        VALIDATE_OR_GOTO (this, out);
        VALIDATE_OR_GOTO (linkname, out);
        VALIDATE_OR_GOTO (loc, out);

        priv = this->private;
        VALIDATE_OR_GOTO (priv, out);

        MAKE_REAL_PATH (real_path, this, loc->path);

        op_ret = posix_lstat_with_gfid (this, real_path, &stbuf);
        if ((op_ret == -1) && (errno == ENOENT)){
                was_present = 0;
        }

        gid = frame->root->gid;

        op_ret = setgid_override (this, real_path, &gid);
        if (op_ret < 0) {
                op_errno = -op_ret;
                op_ret = -1;
                goto out;
        }

        SET_FS_ID (frame->root->uid, gid);
        pathdup = gf_strdup (real_path);
        GF_VALIDATE_OR_GOTO (this->name, pathdup, out);

        parentpath = dirname (pathdup);

        op_ret = posix_lstat_with_gfid (this, parentpath, &preparent);
        if (op_ret == -1) {
                op_errno = errno;
                gf_log (this->name, GF_LOG_ERROR,
                        "pre-operation lstat on parent of %s failed: %s",
                        loc->path, strerror (op_errno));
                goto out;
        }

        op_ret = symlink (linkname, real_path);

        if (op_ret == -1) {
                op_errno = errno;
                gf_log (this->name, GF_LOG_ERROR,
                        "symlink of %s --> %s failed: %s",
                        loc->path, linkname, strerror (op_errno));
                goto out;
        }

        op_ret = posix_gfid_set (this, real_path, params);

#ifndef HAVE_SET_FSID
        op_ret = lchown (real_path, frame->root->uid, gid);
        if (op_ret == -1) {
                op_errno = errno;
                gf_log (this->name, GF_LOG_ERROR,
                        "lchown failed on %s: %s",
                        loc->path, strerror (op_errno));
                goto out;
        }
#endif
        op_ret = posix_lstat_with_gfid (this, real_path, &stbuf);
        if (op_ret == -1) {
                op_errno = errno;
                gf_log (this->name, GF_LOG_ERROR,
                        "lstat failed on %s: %s",
                        loc->path, strerror (op_errno));
                goto out;
        }

        op_ret = posix_lstat_with_gfid (this, parentpath, &postparent);
        if (op_ret == -1) {
                op_errno = errno;
                gf_log (this->name, GF_LOG_ERROR,
                        "post-operation lstat on parent of %s failed: %s",
                        loc->path, strerror (op_errno));
                goto out;
        }
 
        op_ret = 0;

 out:
        if (pathdup)
                GF_FREE (pathdup);

        SET_TO_OLD_FS_ID ();

        STACK_UNWIND_STRICT (symlink, frame, op_ret, op_errno,
                             (loc)?loc->inode:NULL, &stbuf, &preparent, &postparent);

        if ((op_ret == -1) && (!was_present)) {
                unlink (real_path);
        }

        return 0;
}


int
posix_rename (call_frame_t *frame, xlator_t *this,
              loc_t *oldloc, loc_t *newloc)
{
        int32_t               op_ret       = -1;
        int32_t               op_errno     = 0;
        char                 *real_oldpath = NULL;
        char                 *real_newpath = NULL;
        struct iatt           stbuf        = {0, };
        struct posix_private *priv         = NULL;
        char                  was_present  = 1; 
        char                 *oldpathdup    = NULL;
        char                 *oldparentpath = NULL;
        char                 *newpathdup    = NULL;
        char                 *newparentpath = NULL;
        struct iatt           preoldparent  = {0, };
        struct iatt           postoldparent = {0, };
        struct iatt           prenewparent  = {0, };
        struct iatt           postnewparent = {0, };

        DECLARE_OLD_FS_ID_VAR;

        VALIDATE_OR_GOTO (frame, out);
        VALIDATE_OR_GOTO (this, out);
        VALIDATE_OR_GOTO (oldloc, out);
        VALIDATE_OR_GOTO (newloc, out);

        priv = this->private;
        VALIDATE_OR_GOTO (priv, out);

        SET_FS_ID (frame->root->uid, frame->root->gid);
        MAKE_REAL_PATH (real_oldpath, this, oldloc->path);
        MAKE_REAL_PATH (real_newpath, this, newloc->path);

        oldpathdup = gf_strdup (real_oldpath);
        GF_VALIDATE_OR_GOTO (this->name, oldpathdup, out);

        oldparentpath = dirname (oldpathdup);

        op_ret = posix_lstat_with_gfid (this, oldparentpath, &preoldparent);
        if (op_ret == -1) {
                op_errno = errno;
                gf_log (this->name, GF_LOG_ERROR,
                        "pre-operation lstat on parent of %s failed: %s",
                        oldloc->path, strerror (op_errno));
                goto out;
        }

        newpathdup = gf_strdup (real_newpath);
        GF_VALIDATE_OR_GOTO (this->name, newpathdup, out);

        newparentpath = dirname (newpathdup);

        op_ret = posix_lstat_with_gfid (this, newparentpath, &prenewparent);
        if (op_ret == -1) {
                op_errno = errno;
                gf_log (this->name, GF_LOG_ERROR,
                        "pre-operation lstat on parent of %s failed: %s",
                      newloc->path, strerror (op_errno));
                goto out;
        }

        op_ret = posix_lstat_with_gfid (this, real_newpath, &stbuf);
        if ((op_ret == -1) && (errno == ENOENT)){
                was_present = 0;
        }

        op_ret = rename (real_oldpath, real_newpath);
        if (op_ret == -1) {
                op_errno = errno;
                gf_log (this->name,
			(op_errno == ENOTEMPTY ? GF_LOG_DEBUG : GF_LOG_ERROR),
                        "rename of %s to %s failed: %s",
                        oldloc->path, newloc->path, strerror (op_errno));
                goto out;
        }

        op_ret = posix_lstat_with_gfid (this, real_newpath, &stbuf);
        if (op_ret == -1) {
                op_errno = errno;
                gf_log (this->name, GF_LOG_ERROR,
                        "lstat on %s failed: %s",
                        real_newpath, strerror (op_errno));
                goto out;
        }

        op_ret = posix_lstat_with_gfid (this, oldparentpath, &postoldparent);
        if (op_ret == -1) {
                op_errno = errno;
                gf_log (this->name, GF_LOG_ERROR,
                        "post-operation lstat on parent of %s failed: %s",
                        oldloc->path, strerror (op_errno));
                goto out;
        }

        op_ret = posix_lstat_with_gfid (this, newparentpath, &postnewparent);
        if (op_ret == -1) {
                op_errno = errno;
                gf_log (this->name, GF_LOG_ERROR,
                        "post-operation lstat on parent of %s failed: %s",
                        newloc->path, strerror (op_errno));
                goto out;
        }

        op_ret = 0;

 out:
        if (oldpathdup)
                GF_FREE (oldpathdup);

        if (newpathdup)
                GF_FREE (newpathdup);


        SET_TO_OLD_FS_ID ();

        STACK_UNWIND_STRICT (rename, frame, op_ret, op_errno, &stbuf,
                             &preoldparent, &postoldparent,
                             &prenewparent, &postnewparent);

        if ((op_ret == -1) && !was_present) {
                unlink (real_newpath);
        } 

        return 0;
}


int
posix_link (call_frame_t *frame, xlator_t *this,
            loc_t *oldloc, loc_t *newloc)
{
        int32_t               op_ret       = -1;
        int32_t               op_errno     = 0;
        char                 *real_oldpath = 0;
        char                 *real_newpath = 0;
        struct iatt           stbuf        = {0, };
        struct posix_private *priv         = NULL;
        char                  was_present  = 1;
        char                 *newpathdup   = NULL;
        char                 *newparentpath = NULL;
        struct iatt           preparent = {0,};
        struct iatt           postparent = {0,};

        DECLARE_OLD_FS_ID_VAR;

        VALIDATE_OR_GOTO (frame, out);
        VALIDATE_OR_GOTO (this, out);
        VALIDATE_OR_GOTO (oldloc, out);
        VALIDATE_OR_GOTO (newloc, out);

        priv = this->private;
        VALIDATE_OR_GOTO (priv, out);

        SET_FS_ID (frame->root->uid, frame->root->gid);
        MAKE_REAL_PATH (real_oldpath, this, oldloc->path);
        MAKE_REAL_PATH (real_newpath, this, newloc->path);

        op_ret = posix_lstat_with_gfid (this, real_newpath, &stbuf);
        if ((op_ret == -1) && (errno == ENOENT)) {
                was_present = 0;
        }

        newpathdup  = gf_strdup (real_newpath);
        if (!newpathdup) {
                gf_log (this->name, GF_LOG_ERROR, "strdup failed");
                op_errno = ENOMEM;
                goto out;
        }

        newparentpath = dirname (newpathdup);
        op_ret = posix_lstat_with_gfid (this, newparentpath, &preparent);
        if (op_ret == -1) {
                op_errno = errno;
                gf_log (this->name, GF_LOG_ERROR, "lstat failed: %s: %s",
                        newparentpath, strerror (op_errno));
                goto out;
        }

        op_ret = link (real_oldpath, real_newpath);
        if (op_ret == -1) {
                op_errno = errno;
                gf_log (this->name, GF_LOG_ERROR,
                        "link %s to %s failed: %s",
                        oldloc->path, newloc->path, strerror (op_errno));
                goto out;
        }

        op_ret = posix_lstat_with_gfid (this, real_newpath, &stbuf);
        if (op_ret == -1) {
                op_errno = errno;
                gf_log (this->name, GF_LOG_ERROR,
                        "lstat on %s failed: %s",
                        real_newpath, strerror (op_errno));
                goto out;
        }

        op_ret = posix_lstat_with_gfid (this, newparentpath, &postparent);
        if (op_ret == -1) {
                op_errno = errno;
                gf_log (this->name, GF_LOG_ERROR, "lstat failed: %s: %s",
                        newparentpath, strerror (op_errno));
                goto out;
        }

        op_ret = 0;

 out:
        if (newpathdup)
                GF_FREE (newpathdup);
        SET_TO_OLD_FS_ID ();

        STACK_UNWIND_STRICT (link, frame, op_ret, op_errno,
                             (oldloc)?oldloc->inode:NULL, &stbuf, &preparent,
                             &postparent);

        if ((op_ret == -1) && (!was_present)) {
                unlink (real_newpath);
        }

        return 0;
}


int32_t
posix_truncate (call_frame_t *frame,
                xlator_t *this,
                loc_t *loc,
                off_t offset)
{
        int32_t               op_ret    = -1;
        int32_t               op_errno  = 0;
        char                 *real_path = 0;
        struct posix_private *priv      = NULL;
        struct iatt           prebuf    = {0,};
        struct iatt           postbuf   = {0,};

        DECLARE_OLD_FS_ID_VAR;

        VALIDATE_OR_GOTO (frame, out);
        VALIDATE_OR_GOTO (this, out);
        VALIDATE_OR_GOTO (loc, out);

        priv = this->private;
        VALIDATE_OR_GOTO (priv, out);

        SET_FS_ID (frame->root->uid, frame->root->gid);
        MAKE_REAL_PATH (real_path, this, loc->path);

        op_ret = posix_lstat_with_gfid (this, real_path, &prebuf);
        if (op_ret == -1) {
                op_errno = errno;
                gf_log (this->name, GF_LOG_ERROR,
                        "pre-operation lstat on %s failed: %s",
                        loc->path, strerror (op_errno));
                goto out;
        }

        op_ret = truncate (real_path, offset);
        if (op_ret == -1) {
                op_errno = errno;
                gf_log (this->name, GF_LOG_ERROR,
                        "truncate on %s failed: %s",
                        loc->path, strerror (op_errno));
                goto out;
        }

        op_ret = posix_lstat_with_gfid (this, real_path, &postbuf);
        if (op_ret == -1) {
                op_errno = errno;
                gf_log (this->name, GF_LOG_ERROR, "lstat on %s failed: %s",
                        real_path, strerror (op_errno));
                goto out;
        }

        op_ret = 0;

 out:
        SET_TO_OLD_FS_ID ();

        STACK_UNWIND_STRICT (truncate, frame, op_ret, op_errno,
                             &prebuf, &postbuf);

        return 0;
}


int32_t
posix_create (call_frame_t *frame, xlator_t *this,
              loc_t *loc, int32_t flags, mode_t mode,
              fd_t *fd, dict_t *params)
{
        int32_t                op_ret      = -1;
        int32_t                op_errno    = 0;
        int32_t                _fd         = -1;
        int                    _flags      = 0;
        char *                 real_path   = NULL;
        struct iatt            stbuf       = {0, };
        struct posix_fd *      pfd         = NULL;
        struct posix_private * priv        = NULL;
        char                   was_present = 1;  

        gid_t                  gid         = 0;
        char                  *pathdup   = NULL;
        char                  *parentpath = NULL;
        struct iatt            preparent = {0,};
        struct iatt            postparent = {0,};

        DECLARE_OLD_FS_ID_VAR;

        VALIDATE_OR_GOTO (frame, out);
        VALIDATE_OR_GOTO (this, out);
        VALIDATE_OR_GOTO (this->private, out);
        VALIDATE_OR_GOTO (loc, out);
        VALIDATE_OR_GOTO (fd, out);

        priv = this->private;
        VALIDATE_OR_GOTO (priv, out);

        MAKE_REAL_PATH (real_path, this, loc->path);

        gid = frame->root->gid;

        op_ret = setgid_override (this, real_path, &gid);
        if (op_ret < 0) {
                op_errno = -op_ret;
                op_ret = -1;
                goto out;
        }

        SET_FS_ID (frame->root->uid, gid);
        pathdup = gf_strdup (real_path);
        GF_VALIDATE_OR_GOTO (this->name, pathdup, out);

        parentpath = dirname (pathdup);

        op_ret = posix_lstat_with_gfid (this, parentpath, &preparent);
        if (op_ret == -1) {
                op_errno = errno;
                gf_log (this->name, GF_LOG_ERROR,
                        "pre-operation lstat on parent of %s failed: %s",
                        loc->path, strerror (op_errno));
                goto out;
        }

        if (!flags) {
                _flags = O_CREAT | O_RDWR | O_EXCL;
        }
        else {
                _flags = flags | O_CREAT;
        }

        op_ret = posix_lstat_with_gfid (this, real_path, &stbuf);
        if ((op_ret == -1) && (errno == ENOENT)) {
                was_present = 0;
        }

        if (priv->o_direct)
                _flags |= O_DIRECT;

        _fd = open (real_path, _flags, mode);

        if (_fd == -1) {
                op_errno = errno;
                op_ret = -1;
                gf_log (this->name, GF_LOG_ERROR,
                        "open on %s failed: %s", loc->path,
                        strerror (op_errno));
                goto out;
        }

        op_ret = posix_gfid_set (this, real_path, params);

#ifndef HAVE_SET_FSID
        op_ret = chown (real_path, frame->root->uid, gid);
        if (op_ret == -1) {
                op_errno = errno;
                gf_log (this->name, GF_LOG_ERROR,
                        "chown on %s failed: %s",
			real_path, strerror (op_errno));
        }
#endif

        op_ret = posix_fstat_with_gfid (this, _fd, &stbuf);
        if (op_ret == -1) {
                op_errno = errno;
                gf_log (this->name, GF_LOG_ERROR,
                        "fstat on %d failed: %s", _fd, strerror (op_errno));
                goto out;
        }

        op_ret = posix_lstat_with_gfid (this, parentpath, &postparent);
        if (op_ret == -1) {
                op_errno = errno;
                gf_log (this->name, GF_LOG_ERROR,
                        "post-operation lstat on parent of %s failed: %s",
                        loc->path, strerror (op_errno));
                goto out;
        }

	op_ret = -1;
        pfd = GF_CALLOC (1, sizeof (*pfd), gf_posix_mt_posix_fd);

        if (!pfd) {
                op_errno = errno;
                gf_log (this->name, GF_LOG_ERROR,
                        "Out of memory.");
                goto out;
        }

        pfd->flags = flags;
        pfd->fd    = _fd;

	fd_ctx_set (fd, this, (uint64_t)(long)pfd);

        LOCK (&priv->lock);
        {
                priv->nr_files++;
        }
        UNLOCK (&priv->lock);

        op_ret = 0;

 out:
        if (pathdup)
                GF_FREE (pathdup);
        SET_TO_OLD_FS_ID ();

        if ((-1 == op_ret) && (_fd != -1)) {
                close (_fd);

                if (!was_present) {
                        unlink (real_path);
                }
        }

        STACK_UNWIND_STRICT (create, frame, op_ret, op_errno,
                             fd, (loc)?loc->inode:NULL, &stbuf, &preparent,
                             &postparent);

        return 0;
}

int32_t
posix_open (call_frame_t *frame, xlator_t *this,
            loc_t *loc, int32_t flags, fd_t *fd, int wbflags)
{
        int32_t               op_ret       = -1;
        int32_t               op_errno     = 0;
        char                 *real_path    = NULL;
        int32_t               _fd          = -1;
        struct posix_fd      *pfd          = NULL;
        struct posix_private *priv         = NULL;
        char                  was_present  = 1;
        gid_t                 gid          = 0;
        struct iatt           stbuf        = {0, };

        DECLARE_OLD_FS_ID_VAR;

        VALIDATE_OR_GOTO (frame, out);
        VALIDATE_OR_GOTO (this, out);
        VALIDATE_OR_GOTO (this->private, out);
        VALIDATE_OR_GOTO (loc, out);
        VALIDATE_OR_GOTO (fd, out);

        priv = this->private;
        VALIDATE_OR_GOTO (priv, out);

        MAKE_REAL_PATH (real_path, this, loc->path);

        op_ret = setgid_override (this, real_path, &gid);
        if (op_ret < 0) {
                op_errno = -op_ret;
                op_ret = -1;
                goto out;
        }

        SET_FS_ID (frame->root->uid, gid);

        if (priv->o_direct)
                flags |= O_DIRECT;

        op_ret = posix_lstat_with_gfid (this, real_path, &stbuf);
        if ((op_ret == -1) && (errno == ENOENT)) {
                was_present = 0;
        }

        _fd = open (real_path, flags, 0);
        if (_fd == -1) {
                op_ret   = -1;
                op_errno = errno;
                gf_log (this->name, GF_LOG_ERROR,
                        "open on %s: %s", real_path, strerror (op_errno));
                goto out;
        }

        pfd = GF_CALLOC (1, sizeof (*pfd), gf_posix_mt_posix_fd);

        if (!pfd) {
                op_errno = errno;
                gf_log (this->name, GF_LOG_ERROR,
                        "Out of memory.");
                goto out;
        }

        pfd->flags = flags;
        pfd->fd    = _fd;
        if (wbflags == GF_OPEN_FSYNC)
                pfd->flushwrites = 1;

	fd_ctx_set (fd, this, (uint64_t)(long)pfd);

#ifndef HAVE_SET_FSID
        if (flags & O_CREAT) {
                op_ret = chown (real_path, frame->root->uid, gid);
                if (op_ret == -1) {
                        op_errno = errno;
                        gf_log (this->name, GF_LOG_ERROR,
                                "chown on %s failed: %s",
				real_path, strerror (op_errno));
                        goto out;
                }
        }
#endif

        if (flags & O_CREAT) {
                op_ret = posix_lstat_with_gfid (this, real_path, &stbuf);
                if (op_ret == -1) {
                        op_errno = errno;
                        gf_log (this->name, GF_LOG_ERROR, "lstat on (%s) "
                                "failed: %s", real_path, strerror (op_errno));
                        goto out;
                }
        }

        LOCK (&priv->lock);
        {
                priv->nr_files++;
        }
        UNLOCK (&priv->lock);

        op_ret = 0;

 out:
        if (op_ret == -1) {
                if (_fd != -1) {
                        close (_fd);
                }
        }

        SET_TO_OLD_FS_ID ();

        STACK_UNWIND_STRICT (open, frame, op_ret, op_errno, fd);

        return 0;
}

#define ALIGN_BUF(ptr,bound) ((void *)((unsigned long)(ptr + bound - 1) & \
                                       (unsigned long)(~(bound - 1))))

int
posix_readv (call_frame_t *frame, xlator_t *this,
             fd_t *fd, size_t size, off_t offset)
{
	uint64_t               tmp_pfd    = 0;
        int32_t                op_ret     = -1;
        int32_t                op_errno   = 0;
        int                    _fd        = -1;
        struct posix_private * priv       = NULL;
        struct iobuf         * iobuf      = NULL;
        struct iobref        * iobref     = NULL;
        struct iovec           vec        = {0,};
        struct posix_fd *      pfd        = NULL;
        struct iatt            stbuf      = {0,};
        int                    align      = 1;
        int                    ret        = -1;

        VALIDATE_OR_GOTO (frame, out);
        VALIDATE_OR_GOTO (this, out);
        VALIDATE_OR_GOTO (fd, out);
        VALIDATE_OR_GOTO (this->private, out);

        priv = this->private;
        VALIDATE_OR_GOTO (priv, out);

        ret = fd_ctx_get (fd, this, &tmp_pfd);
        if (ret < 0) {
                op_errno = -ret;
                gf_log (this->name, GF_LOG_DEBUG,
			"pfd is NULL from fd=%p", fd);
                goto out;
        }
	pfd = (struct posix_fd *)(long)tmp_pfd;

        if (!size) {
                op_errno = EINVAL;
                gf_log (this->name, GF_LOG_DEBUG, "size=%"GF_PRI_SIZET, size);
                goto out;
        }

        if (pfd->flags & O_DIRECT) {
                align = 4096;    /* align to page boundary */
        }

        iobuf = iobuf_get (this->ctx->iobuf_pool);
        if (!iobuf) {
                gf_log (this->name, GF_LOG_ERROR,
                        "Out of memory.");
                goto out;
        }

        _fd = pfd->fd;
        op_ret = pread (_fd, iobuf->ptr, size, offset);
        if (op_ret == -1) {
                op_errno = errno;
                gf_log (this->name, GF_LOG_ERROR,
                        "read failed on fd=%p: %s", fd,
                        strerror (op_errno));
                goto out;
        }

        LOCK (&priv->lock);
        {
                priv->read_value    += op_ret;
        }
        UNLOCK (&priv->lock);

        vec.iov_base = iobuf->ptr;
        vec.iov_len  = op_ret;

        iobref = iobref_new ();

        iobref_add (iobref, iobuf);

        /*
         *  readv successful, and we need to get the stat of the file
         *  we read from
         */

        op_ret = posix_fstat_with_gfid (this, _fd, &stbuf);
        if (op_ret == -1) {
                op_errno = errno;
                gf_log (this->name, GF_LOG_ERROR,
                        "fstat failed on fd=%p: %s", fd,
                        strerror (op_errno));
                goto out;
        }

        /* Hack to notify higher layers of EOF. */
        if (stbuf.ia_size == 0)
                op_errno = ENOENT;
        else if ((offset + vec.iov_len) == stbuf.ia_size)
                op_errno = ENOENT;

        op_ret = vec.iov_len;
out:

        STACK_UNWIND_STRICT (readv, frame, op_ret, op_errno,
                             &vec, 1, &stbuf, iobref);

        if (iobref)
                iobref_unref (iobref);
        if (iobuf)
                iobuf_unref (iobuf);

        return 0;
}


int32_t
__posix_pwritev (int fd, struct iovec *vector, int count, off_t offset)
{
        int32_t         op_ret = 0;
        int             idx = 0;
        int             retval = 0;
        off_t           internal_off = 0;

        if (!vector)
                return -EFAULT;

        internal_off = offset;
        for (idx = 0; idx < count; idx++) {
                retval = pwrite (fd, vector[idx].iov_base, vector[idx].iov_len,
                                 internal_off);
                if (retval == -1) {
                        op_ret = -errno;
                        goto err;
                }
                op_ret += retval;
                internal_off += retval;
        }

err:
        return op_ret;
}


int32_t
__posix_writev (int fd, struct iovec *vector, int count, off_t startoff,
                int odirect)
{
        int32_t         op_ret = 0;
        int             idx = 0;
        int             align = 4096;
        int             max_buf_size = 0;
        int             retval = 0;
        char            *buf = NULL;
        char            *alloc_buf = NULL;
        off_t           internal_off = 0;

        /* Check for the O_DIRECT flag during open() */
        if (!odirect)
                return __posix_pwritev (fd, vector, count, startoff);

        for (idx = 0; idx < count; idx++) {
                if (max_buf_size < vector[idx].iov_len)
                        max_buf_size = vector[idx].iov_len;
        }

        alloc_buf = GF_MALLOC (1 * (max_buf_size + align), gf_posix_mt_char);
        if (!alloc_buf) {
                op_ret = -errno;
                goto err;
        }

        internal_off = startoff;
        for (idx = 0; idx < count; idx++) {
                /* page aligned buffer */
                buf = ALIGN_BUF (alloc_buf, align);

                memcpy (buf, vector[idx].iov_base, vector[idx].iov_len);

                /* not sure whether writev works on O_DIRECT'd fd */
                retval = pwrite (fd, buf, vector[idx].iov_len, internal_off);
                if (retval == -1) {
                        op_ret = -errno;
                        goto err;
                }

                op_ret += retval;
                internal_off += retval;
        }

err:
        if (alloc_buf)
                GF_FREE (alloc_buf);

        return op_ret;
}


int32_t
posix_writev (call_frame_t *frame, xlator_t *this,
              fd_t *fd, struct iovec *vector, int32_t count, off_t offset,
              struct iobref *iobref)
{
        int32_t                op_ret   = -1;
        int32_t                op_errno = 0;
        int                    _fd      = -1;
        struct posix_private * priv     = NULL;
        struct posix_fd *      pfd      = NULL;
        struct iatt            preop    = {0,};
        struct iatt            postop    = {0,};
        int                      ret      = -1;

	uint64_t  tmp_pfd   = 0;

        VALIDATE_OR_GOTO (frame, out);
        VALIDATE_OR_GOTO (this, out);
        VALIDATE_OR_GOTO (fd, out);
        VALIDATE_OR_GOTO (vector, out);
        VALIDATE_OR_GOTO (this->private, out);

        priv = this->private;

        VALIDATE_OR_GOTO (priv, out);

        ret = fd_ctx_get (fd, this, &tmp_pfd);
        if (ret < 0) {
                gf_log (this->name, GF_LOG_DEBUG,
			"pfd is NULL from fd=%p", fd);
                op_errno = -ret;
                goto out;
        }
	pfd = (struct posix_fd *)(long)tmp_pfd;

        _fd = pfd->fd;

        op_ret = posix_fstat_with_gfid (this, _fd, &preop);
        if (op_ret == -1) {
                op_errno = errno;
                gf_log (this->name, GF_LOG_ERROR,
                        "pre-operation fstat failed on fd=%p: %s", fd,
                        strerror (op_errno));
                goto out;
        }

        op_ret = __posix_writev (_fd, vector, count, offset,
                                 (pfd->flags & O_DIRECT));
        if (op_ret < 0) {
                op_errno = -op_ret;
                op_ret = -1;
                gf_log (this->name, GF_LOG_ERROR, "write failed: offset %"PRIu64
                        ", %s", offset, strerror (op_errno));
                goto out;
        }

        LOCK (&priv->lock);
        {
                priv->write_value    += op_ret;
        }
        UNLOCK (&priv->lock);

        if (op_ret >= 0) {
                /* wiretv successful, we also need to get the stat of
                 * the file we wrote to
                 */

                if (pfd->flushwrites) {
                        /* NOTE: ignore the error, if one occurs at this
                         * point */
                        fsync (_fd);
                }

                ret = posix_fstat_with_gfid (this, _fd, &postop);
                if (ret == -1) {
			op_ret = -1;
                        op_errno = errno;
                        gf_log (this->name, GF_LOG_ERROR, 
                                "post-operation fstat failed on fd=%p: %s",
                                fd, strerror (op_errno));
                        goto out;
                }
        }

 out:

        STACK_UNWIND_STRICT (writev, frame, op_ret, op_errno, &preop, &postop);

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
                gf_log (this->name, GF_LOG_ERROR, 
                        "statvfs failed on %s: %s",
                        real_path, strerror (op_errno));
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
        STACK_UNWIND_STRICT (statfs, frame, op_ret, op_errno, &buf);
        return 0;
}


int32_t
posix_flush (call_frame_t *frame, xlator_t *this,
             fd_t *fd)
{
        int32_t           op_ret   = -1;
        int32_t           op_errno = 0;
        struct posix_fd * pfd      = NULL;
        int               ret      = -1;
	uint64_t          tmp_pfd  = 0;

        VALIDATE_OR_GOTO (frame, out);
        VALIDATE_OR_GOTO (this, out);
        VALIDATE_OR_GOTO (fd, out);

        ret = fd_ctx_get (fd, this, &tmp_pfd);
        if (ret < 0) {
                op_errno = -ret;
                gf_log (this->name, GF_LOG_DEBUG,
                        "pfd is NULL on fd=%p", fd);
                goto out;
        }
	pfd = (struct posix_fd *)(long)tmp_pfd;

        op_ret = 0;

 out:
        STACK_UNWIND_STRICT (flush, frame, op_ret, op_errno);

        return 0;
}


int32_t
posix_release (xlator_t *this,
	       fd_t *fd)
{
        struct posix_private * priv     = NULL;
        struct posix_fd *      pfd      = NULL;
        int                    ret      = -1;
	uint64_t               tmp_pfd  = 0;

        VALIDATE_OR_GOTO (this, out);
        VALIDATE_OR_GOTO (fd, out);

        priv = this->private;

        ret = fd_ctx_get (fd, this, &tmp_pfd);
        if (ret < 0) {
                gf_log (this->name, GF_LOG_DEBUG,
                        "pfd is NULL from fd=%p", fd);
                goto out;
        }
	pfd = (struct posix_fd *)(long)tmp_pfd;

        if (pfd->dir) {
                gf_log (this->name, GF_LOG_DEBUG,
                        "pfd->dir is %p (not NULL) for file fd=%p",
                        pfd->dir, fd);
        }

        pthread_mutex_lock (&priv->janitor_lock);
        {
                INIT_LIST_HEAD (&pfd->list);
                list_add_tail (&pfd->list, &priv->janitor_fds);
                pthread_cond_signal (&priv->janitor_cond);
        }
        pthread_mutex_unlock (&priv->janitor_lock);

        LOCK (&priv->lock);
        {
                priv->nr_files--;
        }
        UNLOCK (&priv->lock);

 out:
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
	uint64_t          tmp_pfd  = 0;
        struct iatt       preop = {0,};
        struct iatt       postop = {0,};

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

        ret = fd_ctx_get (fd, this, &tmp_pfd);
        if (ret < 0) {
                op_errno = -ret;
                gf_log (this->name, GF_LOG_DEBUG, 
                        "pfd not found in fd's ctx");
                goto out;
        }
	pfd = (struct posix_fd *)(long)tmp_pfd;

        _fd = pfd->fd;

        op_ret = posix_fstat_with_gfid (this, _fd, &preop);
        if (op_ret == -1) {
                op_errno = errno;
                gf_log (this->name, GF_LOG_DEBUG,
                        "pre-operation fstat failed on fd=%p: %s", fd,
                        strerror (op_errno));
                goto out;
        }

        if (datasync) {
                ;
#ifdef HAVE_FDATASYNC
                op_ret = fdatasync (_fd);
                if (op_ret == -1) {
                        gf_log (this->name, GF_LOG_ERROR,
                                "fdatasync on fd=%p failed: %s",
                                fd, strerror (errno));
                }
#endif
        } else {
                op_ret = fsync (_fd);
                if (op_ret == -1) {
                        op_errno = errno;
                        gf_log (this->name, GF_LOG_ERROR,
                                "fsync on fd=%p failed: %s",
                                fd, strerror (op_errno));
                        goto out;
                }
        }

        op_ret = posix_fstat_with_gfid (this, _fd, &postop);
        if (op_ret == -1) {
                op_errno = errno;
                gf_log (this->name, GF_LOG_DEBUG,
                        "post-operation fstat failed on fd=%p: %s", fd,
                        strerror (op_errno));
                goto out;
        }

        op_ret = 0;

 out:
        SET_TO_OLD_FS_ID ();

        STACK_UNWIND_STRICT (fsync, frame, op_ret, op_errno, &preop, &postop);

        return 0;
}

static int gf_posix_xattr_enotsup_log;

int
set_file_contents (xlator_t *this, char *real_path,
                   data_pair_t *trav, int flags)
{
        char *      key                        = NULL;
        char        real_filepath[ZR_PATH_MAX] = {0,};
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
                        ret = write (file_fd, trav->value->data,
				     trav->value->len);
                        if (ret == -1) {
                                op_ret = -errno;
                                gf_log (this->name, GF_LOG_ERROR,
					"write failed while doing setxattr "
					"for key %s on path %s: %s",
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
                                "write failed on %s while setxattr with "
				"key %s: %s",
                                real_filepath, key, strerror (errno));
                        goto out;
                }

                ret = close (file_fd);
                if (ret == -1) {
                        op_ret = -errno;
                        gf_log (this->name, GF_LOG_ERROR,
                                "close failed on %s while setxattr with "
				"key %s: %s",
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

        if (ZR_FILE_CONTENT_REQUEST(trav->key)) {
                ret = set_file_contents (this, real_path, trav, flags);
        } else {
                sys_ret = sys_lsetxattr (real_path, trav->key,
                                         trav->value->data,
                                         trav->value->len, flags);

                if (sys_ret < 0) {
                        if (errno == ENOTSUP) {
                                GF_LOG_OCCASIONALLY(gf_posix_xattr_enotsup_log,
						    this->name,GF_LOG_WARNING,
						    "Extended attributes not "
						    "supported");
                        } else if (errno == ENOENT) {
                                gf_log (this->name, GF_LOG_ERROR,
                                        "setxattr on %s failed: %s", real_path,
                                        strerror (errno));
                        } else {

#ifdef GF_DARWIN_HOST_OS
				gf_log (this->name,
					((errno == EINVAL) ?
					 GF_LOG_DEBUG : GF_LOG_ERROR),
					"%s: key:%s error:%s",
					real_path, trav->key,
					strerror (errno));
#else /* ! DARWIN */
                                gf_log (this->name, GF_LOG_ERROR,
                                        "%s: key:%s error:%s",
                                        real_path, trav->key,
					strerror (errno));
#endif /* DARWIN */
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

        dict_del (dict, GFID_XATTR_KEY);

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

        STACK_UNWIND_STRICT (setxattr, frame, op_ret, op_errno);

        return 0;
}

int
get_file_contents (xlator_t *this, char *real_path,
                   const char *name, char **contents)
{
        char        real_filepath[ZR_PATH_MAX] = {0,};
        char *      key                        = NULL;
        int32_t     file_fd                    = -1;
        struct iatt stbuf                      = {0,};
        int         op_ret                     = 0;
        int         ret                        = -1;

        key = (char *) &(name[15]);
        sprintf (real_filepath, "%s/%s", real_path, key);

        op_ret = posix_lstat_with_gfid (this, real_filepath, &stbuf);
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

        *contents = GF_CALLOC (stbuf.ia_size + 1, sizeof(char),
                               gf_posix_mt_char);

        if (! *contents) {
                op_ret = -errno;
                gf_log (this->name, GF_LOG_ERROR, "Out of memory.");
                goto out;
        }

        ret = read (file_fd, *contents, stbuf.ia_size);
        if (ret <= 0) {
                op_ret = -1;
                gf_log (this->name, GF_LOG_ERROR, "read on %s failed: %s",
                        real_filepath, strerror (errno));
                goto out;
        }

        *contents[stbuf.ia_size] = '\0';

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
                        GF_FREE (*contents);
                if (file_fd != -1)
                        close (file_fd);
        }

        return op_ret;
}

/**
 * posix_getxattr - this function returns a dictionary with all the
 *                  key:value pair present as xattr. used for
 *                  both 'listxattr' and 'getxattr'.
 */
int32_t
posix_getxattr (call_frame_t *frame, xlator_t *this,
                loc_t *loc, const char *name)
{
        struct posix_private *priv  = NULL;
        int32_t  op_ret         = -1;
        int32_t  op_errno       = 0;
        int32_t  list_offset    = 0;
        size_t   size           = 0;
        size_t   remaining_size = 0;
        char     key[1024]      = {0,};
        char     host_buf[1024] = {0,};
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

        priv = this->private;

        if (loc->inode && IA_ISDIR(loc->inode->ia_type) && name &&
	    ZR_FILE_CONTENT_REQUEST(name)) {
                ret = get_file_contents (this, real_path, name,
					 &file_contents);
                if (ret < 0) {
                        op_errno = -ret;
                        gf_log (this->name, GF_LOG_ERROR,
				"getting file contents failed: %s",
                                strerror (op_errno));
                        goto out;
                }
        }

        /* Get the total size */
        dict = get_new_dict ();
        if (!dict) {
                gf_log (this->name, GF_LOG_ERROR, "Out of memory.");
                goto out;
        }

        if (loc->inode && name && !strcmp (name, GLUSTERFS_OPEN_FD_COUNT)) {
		if (!list_empty (&loc->inode->fd_list)) {
			ret = dict_set_uint32 (dict, (char *)name, 1);
                        if (ret < 0)
                                gf_log (this->name, GF_LOG_WARNING,
                                        "Failed to set dictionary value for %s",
                                        name);
		} else {
			ret = dict_set_uint32 (dict, (char *)name, 0);
                        if (ret < 0)
                                gf_log (this->name, GF_LOG_WARNING,
                                        "Failed to set dictionary value for %s",
                                        name);
		}
                goto done;
        }
	if (loc->inode && IA_ISREG (loc->inode->ia_type) && name &&
	    (strcmp (name, GF_XATTR_PATHINFO_KEY) == 0)) {
                snprintf (host_buf, 1024, "%s:%s", priv->hostname,
                          real_path);
                ret = dict_set_str (dict, GF_XATTR_PATHINFO_KEY,
                                    host_buf);
                if (ret < 0) {
                        gf_log (this->name, GF_LOG_WARNING,
                                "could not set value (%s) in dictionary",
                                host_buf);
                }
                goto done;
	}

        size = sys_llistxattr (real_path, NULL, 0);
        if (size == -1) {
                op_errno = errno;
                if ((errno == ENOTSUP) || (errno == ENOSYS)) {
                        GF_LOG_OCCASIONALLY (gf_posix_xattr_enotsup_log,
                                             this->name, GF_LOG_WARNING,
                                             "Extended attributes not "
					     "supported.");
                }
                else {
                        gf_log (this->name, GF_LOG_ERROR,
				"listxattr failed on %s: %s",
                                real_path, strerror (op_errno));
                }
                goto out;
        }

        if (size == 0)
                goto done;

        list = alloca (size + 1);
        if (!list) {
                op_errno = errno;
                gf_log (this->name, GF_LOG_ERROR, "Out of memory.");
                goto out;
        }

        size = sys_llistxattr (real_path, list, size);

        remaining_size = size;
        list_offset = 0;
        while (remaining_size > 0) {
                if (*(list + list_offset) == '\0')
                        break;

                strcpy (key, list + list_offset);
                op_ret = sys_lgetxattr (real_path, key, NULL, 0);
                if (op_ret == -1)
                        break;

                value = GF_CALLOC (op_ret + 1, sizeof(char),
                                   gf_posix_mt_char);
                if (!value) {
                        op_errno = errno;
                        gf_log (this->name, GF_LOG_ERROR, "Out of memory.");
                        goto out;
                }

                op_ret = sys_lgetxattr (real_path, key, value, op_ret);
                if (op_ret == -1) {
                        op_errno = errno;

                        break;
                }

                value [op_ret] = '\0';
                dict_set (dict, key, data_from_dynptr (value, op_ret));

                remaining_size -= strlen (key) + 1;
                list_offset += strlen (key) + 1;

        } /* while (remaining_size > 0) */

 done:
        op_ret = size;

        if (dict) {
                dict_del (dict, GFID_XATTR_KEY);
                dict_ref (dict);
        }

 out:
        SET_TO_OLD_FS_ID ();

        STACK_UNWIND_STRICT (getxattr, frame, op_ret, op_errno, dict);

        if (dict)
                dict_unref (dict);

        return 0;
}


int32_t
posix_fgetxattr (call_frame_t *frame, xlator_t *this,
                 fd_t *fd, const char *name)
{
        int32_t           op_ret         = -1;
        int32_t           op_errno       = ENOENT;
        uint64_t          tmp_pfd        = 0;
        struct posix_fd * pfd            = NULL;
        int               _fd            = -1;
        int32_t           list_offset    = 0;
        size_t            size           = 0;
        size_t            remaining_size = 0;
        char              key[1024]      = {0,};
        char *            value          = NULL;
        char *            list           = NULL;
        dict_t *          dict           = NULL;
        int               ret            = -1;

        DECLARE_OLD_FS_ID_VAR;

        VALIDATE_OR_GOTO (frame, out);
        VALIDATE_OR_GOTO (this, out);
        VALIDATE_OR_GOTO (fd, out);

        SET_FS_ID (frame->root->uid, frame->root->gid);

        ret = fd_ctx_get (fd, this, &tmp_pfd);
        if (ret < 0) {
                op_errno = -ret;
                gf_log (this->name, GF_LOG_DEBUG,
			"pfd is NULL from fd=%p", fd);
                goto out;
        }
	pfd = (struct posix_fd *)(long)tmp_pfd;

        _fd = pfd->fd;

        /* Get the total size */
        dict = get_new_dict ();
        if (!dict) {
                gf_log (this->name, GF_LOG_ERROR, "Out of memory.");
                goto out;
        }

        if (name && !strcmp (name, GLUSTERFS_OPEN_FD_COUNT)) {
                ret = dict_set_uint32 (dict, (char *)name, 1);
                if (ret < 0)
                        gf_log (this->name, GF_LOG_WARNING,
                                "Failed to set dictionary value for %s",
                                name);
                goto done;
        }

        size = sys_flistxattr (_fd, NULL, 0);
        if (size == -1) {
                op_errno = errno;
                if ((errno == ENOTSUP) || (errno == ENOSYS)) {
                        GF_LOG_OCCASIONALLY (gf_posix_xattr_enotsup_log,
                                             this->name, GF_LOG_WARNING,
                                             "Extended attributes not "
					     "supported.");
                }
                else {
                        gf_log (this->name, GF_LOG_ERROR,
				"listxattr failed on %p: %s",
                                fd, strerror (op_errno));
                }
                goto out;
        }

        if (size == 0)
                goto done;

        list = alloca (size + 1);
        if (!list) {
                op_errno = errno;
                gf_log (this->name, GF_LOG_ERROR, "Out of memory.");
                goto out;
        }

        size = sys_flistxattr (_fd, list, size);

        remaining_size = size;
        list_offset = 0;
        while (remaining_size > 0) {
                if(*(list + list_offset) == '\0')
                        break;

                strcpy (key, list + list_offset);
                op_ret = sys_fgetxattr (_fd, key, NULL, 0);
                if (op_ret == -1)
                        break;

                value = GF_CALLOC (op_ret + 1, sizeof(char),
                                   gf_posix_mt_char);
                if (!value) {
                        op_errno = errno;
                        gf_log (this->name, GF_LOG_ERROR, "Out of memory.");
                        goto out;
                }

                op_ret = sys_fgetxattr (_fd, key, value, op_ret);
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
                dict_del (dict, GFID_XATTR_KEY);
                dict_ref (dict);
        }

 out:
        SET_TO_OLD_FS_ID ();

        STACK_UNWIND_STRICT (fgetxattr, frame, op_ret, op_errno, dict);

        if (dict)
                dict_unref (dict);

        return 0;
}


int
fhandle_pair (xlator_t *this, int fd,
              data_pair_t *trav, int flags)
{
        int sys_ret = -1;
        int ret     = 0;

        sys_ret = sys_fsetxattr (fd, trav->key, trav->value->data,
                                 trav->value->len, flags);
        
        if (sys_ret < 0) {
                if (errno == ENOTSUP) {
                        GF_LOG_OCCASIONALLY(gf_posix_xattr_enotsup_log,
                                            this->name,GF_LOG_WARNING,
                                            "Extended attributes not "
                                            "supported");
                } else if (errno == ENOENT) {
                        gf_log (this->name, GF_LOG_ERROR,
                                "fsetxattr on fd=%d failed: %s", fd,
                                strerror (errno));
                } else {
                        
#ifdef GF_DARWIN_HOST_OS
                        gf_log (this->name,
                                ((errno == EINVAL) ?
                                 GF_LOG_DEBUG : GF_LOG_ERROR),
                                "fd=%d: key:%s error:%s",
                                fd, trav->key,
                                strerror (errno));
#else /* ! DARWIN */
                        gf_log (this->name, GF_LOG_ERROR,
                                "fd=%d: key:%s error:%s",
                                fd, trav->key,
                                strerror (errno));
#endif /* DARWIN */
                }
                
                ret = -errno;
                goto out;
        }

out:
        return ret;
}


int32_t
posix_fsetxattr (call_frame_t *frame, xlator_t *this,
                 fd_t *fd, dict_t *dict, int flags)
{
        int32_t            op_ret       = -1;
        int32_t            op_errno     = 0;
        struct posix_fd *  pfd          = NULL;
        uint64_t           tmp_pfd      = 0;
        int                _fd          = -1;
        data_pair_t * trav              = NULL;
        int           ret               = -1;

        DECLARE_OLD_FS_ID_VAR;
        SET_FS_ID (frame->root->uid, frame->root->gid);

        VALIDATE_OR_GOTO (frame, out);
        VALIDATE_OR_GOTO (this, out);
        VALIDATE_OR_GOTO (fd, out);
        VALIDATE_OR_GOTO (dict, out);

        ret = fd_ctx_get (fd, this, &tmp_pfd);
        if (ret < 0) {
                op_errno = -ret;
                gf_log (this->name, GF_LOG_DEBUG,
			"pfd is NULL from fd=%p", fd);
                goto out;
        }
	pfd = (struct posix_fd *)(long)tmp_pfd;
        _fd = pfd->fd;

        dict_del (dict, GFID_XATTR_KEY);

        trav = dict->members_list;

        while (trav) {
                ret = fhandle_pair (this, _fd, trav, flags);
                if (ret < 0) {
                        op_errno = -ret;
                        goto out;
                }
                trav = trav->next;
        }

        op_ret = 0;

 out:
        SET_TO_OLD_FS_ID ();

        STACK_UNWIND_STRICT (fsetxattr, frame, op_ret, op_errno);

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

        op_ret = sys_lremovexattr (real_path, name);

        if (op_ret == -1) {
                op_errno = errno;
		if (op_errno != ENOATTR && op_errno != EPERM)
			gf_log (this->name, GF_LOG_ERROR,
				"removexattr on %s: %s", loc->path,
                        strerror (op_errno));
                goto out;
        }

        op_ret = 0;

 out:
        SET_TO_OLD_FS_ID ();

        STACK_UNWIND_STRICT (removexattr, frame, op_ret, op_errno);
        return 0;
}


int32_t
posix_fsyncdir (call_frame_t *frame, xlator_t *this,
                fd_t *fd, int datasync)
{
        int32_t           op_ret   = -1;
        int32_t           op_errno = 0;
        struct posix_fd * pfd      = NULL;
        int               ret      = -1;
	uint64_t          tmp_pfd  = 0;

        VALIDATE_OR_GOTO (frame, out);
        VALIDATE_OR_GOTO (this, out);
        VALIDATE_OR_GOTO (fd, out);

        ret = fd_ctx_get (fd, this, &tmp_pfd);
        if (ret < 0) {
                op_errno = -ret;
                gf_log (this->name, GF_LOG_DEBUG,
                        "pfd is NULL, fd=%p", fd);
                goto out;
        }
	pfd = (struct posix_fd *)(long)tmp_pfd;

        op_ret = 0;

 out:
        STACK_UNWIND_STRICT (fsyncdir, frame, op_ret, op_errno);

        return 0;
}


void
posix_print_xattr (dict_t *this,
		   char *key,
		   data_t *value,
		   void *data)
{
	gf_log ("posix", GF_LOG_DEBUG,
		"(key/val) = (%s/%d)", key, data_to_int32 (value));
}


/**
 * add_array - add two arrays of 32-bit numbers (stored in network byte order)
 * dest = dest + src
 * @count: number of 32-bit numbers
 * FIXME: handle overflow
 */

static void
__add_array (int32_t *dest, int32_t *src, int count)
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
 *               "key" ==> array of 32-bit numbers
 */

int
do_xattrop (call_frame_t *frame, xlator_t *this,
            loc_t *loc, fd_t *fd, gf_xattrop_flags_t optype, dict_t *xattr)
{
	char            *real_path = NULL;
	int32_t         *array = NULL;
	int              size = 0;
	int              count = 0;

	int              op_ret = 0;
	int              op_errno = 0;

        int              ret = 0;
	int              _fd = -1;
        uint64_t         tmp_pfd = 0;
	struct posix_fd *pfd = NULL;

	data_pair_t     *trav = NULL;

        char *    path  = NULL;
        inode_t * inode = NULL;

	VALIDATE_OR_GOTO (frame, out);
	VALIDATE_OR_GOTO (xattr, out);
	VALIDATE_OR_GOTO (this, out);

	trav = xattr->members_list;

	if (fd) {
		ret = fd_ctx_get (fd, this, &tmp_pfd);
		if (ret < 0) {
			gf_log (this->name, GF_LOG_DEBUG,
				"failed to get pfd from fd=%p",
				fd);
			op_ret = -1;
			op_errno = EBADFD;
			goto out;
		}
                pfd = (struct posix_fd *)(long)tmp_pfd;
		_fd = pfd->fd;
	}

	if (loc && loc->path)
		MAKE_REAL_PATH (real_path, this, loc->path);

        if (loc) {
                path  = gf_strdup (loc->path);
                inode = loc->inode;
        } else if (fd) {
                inode = fd->inode;
        }

	while (trav && inode) {
		count = trav->value->len / sizeof (int32_t);
		array = GF_CALLOC (count, sizeof (int32_t),
                                   gf_posix_mt_int32_t);

                LOCK (&inode->lock);
                {
                        if (loc) {
                                size = sys_lgetxattr (real_path, trav->key, (char *)array, 
                                                      trav->value->len);
                        } else {
                                size = sys_fgetxattr (_fd, trav->key, (char *)array, 
                                                      trav->value->len);
                        }

                        op_errno = errno;
                        if ((size == -1) && (op_errno != ENODATA) && 
                            (op_errno != ENOATTR)) {
                                if (op_errno == ENOTSUP) {
                                        GF_LOG_OCCASIONALLY(gf_posix_xattr_enotsup_log,
                                                            this->name,GF_LOG_WARNING, 
                                                            "Extended attributes not "
                                                            "supported by filesystem");
                                } else 	{
                                        if (loc)
                                                gf_log (this->name, GF_LOG_ERROR,
                                                        "getxattr failed on %s while doing "
                                                        "xattrop: %s", path,
                                                        strerror (op_errno));
                                        else
                                                gf_log (this->name, GF_LOG_ERROR,
                                                        "fgetxattr failed on fd=%d while doing "
                                                        "xattrop: %s", _fd,
                                                        strerror (op_errno));
                                }

                                op_ret = -1;
                                goto unlock;
                        }

                        switch (optype) {

                        case GF_XATTROP_ADD_ARRAY:
                                __add_array (array, (int32_t *) trav->value->data, 
                                             trav->value->len / 4);
                                break;

                        default:
                                gf_log (this->name, GF_LOG_ERROR,
                                        "Unknown xattrop type (%d) on %s. Please send "
                                        "a bug report to gluster-devel@nongnu.org",
                                        optype, path);
                                op_ret = -1;
                                op_errno = EINVAL;
                                goto unlock;
                        }

                        if (loc) {
                                size = sys_lsetxattr (real_path, trav->key, array,
                                                      trav->value->len, 0);
                        } else {
                                size = sys_fsetxattr (_fd, trav->key, (char *)array,
                                                      trav->value->len, 0);
                        }
                }
        unlock:
                UNLOCK (&inode->lock);

                if (op_ret == -1)
                        goto out;

		op_errno = errno;
		if (size == -1) {
                        if (loc)
                                gf_log (this->name, GF_LOG_ERROR,
                                        "setxattr failed on %s while doing xattrop: "
                                        "key=%s (%s)", path,
                                        trav->key, strerror (op_errno));
                        else
                                gf_log (this->name, GF_LOG_ERROR,
                                        "fsetxattr failed on fd=%d while doing xattrop: "
                                        "key=%s (%s)", _fd,
                                        trav->key, strerror (op_errno));

			op_ret = -1;
			goto out;
		} else {
			size = dict_set_bin (xattr, trav->key, array, 
					     trav->value->len);

			if (size != 0) {
                                if (loc)
                                        gf_log (this->name, GF_LOG_DEBUG,
                                                "dict_set_bin failed (path=%s): "
                                                "key=%s (%s)", path,
                                                trav->key, strerror (-size));
                                else
                                        gf_log (this->name, GF_LOG_DEBUG,
                                                "dict_set_bin failed (fd=%d): "
                                                "key=%s (%s)", _fd,
                                                trav->key, strerror (-size));

				op_ret = -1;
				op_errno = EINVAL;
				goto out;
			}
			array = NULL;
		}

		array = NULL;
		trav = trav->next;
	}

out:
	if (array)
		GF_FREE (array);

        if (path)
                GF_FREE (path);

	STACK_UNWIND_STRICT (xattrop, frame, op_ret, op_errno, xattr);
	return 0;
}


int
posix_xattrop (call_frame_t *frame, xlator_t *this,
               loc_t *loc, gf_xattrop_flags_t optype, dict_t *xattr)
{
        do_xattrop (frame, this, loc, NULL, optype, xattr);
        return 0;
}


int
posix_fxattrop (call_frame_t *frame, xlator_t *this,
		fd_t *fd, gf_xattrop_flags_t optype, dict_t *xattr)
{
        do_xattrop (frame, this, NULL, fd, optype, xattr);
        return 0;
}


int
posix_access (call_frame_t *frame, xlator_t *this,
              loc_t *loc, int32_t mask)
{
        int32_t                 op_ret    = -1;
        int32_t                 op_errno  = 0;
        char                   *real_path = NULL;

        DECLARE_OLD_FS_ID_VAR;
        SET_FS_ID (frame->root->uid, frame->root->gid);

        VALIDATE_OR_GOTO (frame, out);
        VALIDATE_OR_GOTO (this, out);
        VALIDATE_OR_GOTO (loc, out);

        MAKE_REAL_PATH (real_path, this, loc->path);

        op_ret = access (real_path, mask & 07);
        if (op_ret == -1) {
                op_errno = errno;
                gf_log (this->name, GF_LOG_ERROR, "access failed on %s: %s",
                        loc->path, strerror (op_errno));
                goto out;
        }
        op_ret = 0;

 out:
        SET_TO_OLD_FS_ID ();

        STACK_UNWIND_STRICT (access, frame, op_ret, op_errno);
        return 0;
}


int32_t
posix_ftruncate (call_frame_t *frame, xlator_t *this,
                 fd_t *fd, off_t offset)
{
        int32_t               op_ret   = -1;
        int32_t               op_errno = 0;
        int                   _fd      = -1;
        struct iatt           preop    = {0,};
        struct iatt           postop   = {0,};
        struct posix_fd      *pfd      = NULL;
        int                   ret      = -1;
	uint64_t              tmp_pfd  = 0;
        struct posix_private *priv     = NULL;

        DECLARE_OLD_FS_ID_VAR;
        SET_FS_ID (frame->root->uid, frame->root->gid);

        VALIDATE_OR_GOTO (frame, out);
        VALIDATE_OR_GOTO (this, out);
        VALIDATE_OR_GOTO (fd, out);

        priv = this->private;
        VALIDATE_OR_GOTO (priv, out);

        ret = fd_ctx_get (fd, this, &tmp_pfd);
        if (ret < 0) {
                gf_log (this->name, GF_LOG_DEBUG,
                        "pfd is NULL, fd=%p", fd);
                op_errno = -ret;
                goto out;
        }
	pfd = (struct posix_fd *)(long)tmp_pfd;

        _fd = pfd->fd;

        op_ret = posix_fstat_with_gfid (this, _fd, &preop);
        if (op_ret == -1) {
                op_errno = errno;
                gf_log (this->name, GF_LOG_ERROR,
                        "pre-operation fstat failed on fd=%p: %s", fd,
                        strerror (op_errno));
                goto out;
        }

        op_ret = ftruncate (_fd, offset);

        if (op_ret == -1) {
                op_errno = errno;
                gf_log (this->name, GF_LOG_ERROR, 
                        "ftruncate failed on fd=%p: %s",
                        fd, strerror (errno));
                goto out;
        }

        op_ret = posix_fstat_with_gfid (this, _fd, &postop);
        if (op_ret == -1) {
                op_errno = errno;
                gf_log (this->name, GF_LOG_ERROR,
                        "post-operation fstat failed on fd=%p: %s",
                        fd, strerror (errno));
                goto out;
        }

        op_ret = 0;

 out:
        SET_TO_OLD_FS_ID ();

        STACK_UNWIND_STRICT (ftruncate, frame, op_ret, op_errno, &preop, &postop);

        return 0;
}


int32_t
posix_fstat (call_frame_t *frame, xlator_t *this,
             fd_t *fd)
{
        int                   _fd      = -1;
        int32_t               op_ret   = -1;
        int32_t               op_errno = 0;
        struct iatt           buf      = {0,};
        struct posix_fd      *pfd      = NULL;
	uint64_t              tmp_pfd  = 0;
        int                   ret      = -1;
        struct posix_private *priv     = NULL; 

        DECLARE_OLD_FS_ID_VAR;
        SET_FS_ID (frame->root->uid, frame->root->gid);

        VALIDATE_OR_GOTO (frame, out);
        VALIDATE_OR_GOTO (this, out);
        VALIDATE_OR_GOTO (fd, out);

        priv = this->private;
        VALIDATE_OR_GOTO (priv, out);

        ret = fd_ctx_get (fd, this, &tmp_pfd);
        if (ret < 0) {
                gf_log (this->name, GF_LOG_DEBUG,
                        "pfd is NULL, fd=%p", fd);
                op_errno = -ret;
                goto out;
        }
	pfd = (struct posix_fd *)(long)tmp_pfd;

        _fd = pfd->fd;

        op_ret = posix_fstat_with_gfid (this, _fd, &buf);
        if (op_ret == -1) {
                op_errno = errno;
                gf_log (this->name, GF_LOG_ERROR, "fstat failed on fd=%p: %s",
                        fd, strerror (op_errno));
                goto out;
        }

        op_ret = 0;

out:
        SET_TO_OLD_FS_ID ();

        STACK_UNWIND_STRICT (fstat, frame, op_ret, op_errno, &buf);
        return 0;
}

static int gf_posix_lk_log;

int32_t
posix_lk (call_frame_t *frame, xlator_t *this,
          fd_t *fd, int32_t cmd, struct gf_flock *lock)
{
        struct gf_flock nullock = {0, };

        gf_posix_lk_log++;

	GF_LOG_OCCASIONALLY (gf_posix_lk_log, this->name, GF_LOG_ERROR,
			     "\"features/locks\" translator is "
			     "not loaded. You need to use it for proper "
                             "functioning of your application.");

        STACK_UNWIND_STRICT (lk, frame, -1, ENOSYS, &nullock);
        return 0;
}

int32_t
posix_inodelk (call_frame_t *frame, xlator_t *this,
	       const char *volume, loc_t *loc, int32_t cmd, struct gf_flock *lock)
{
	gf_log (this->name, GF_LOG_CRITICAL,
		"\"features/locks\" translator is not loaded. "
		"You need to use it for proper functioning of GlusterFS");

        STACK_UNWIND_STRICT (inodelk, frame, -1, ENOSYS);
        return 0;
}

int32_t
posix_finodelk (call_frame_t *frame, xlator_t *this,
		const char *volume, fd_t *fd, int32_t cmd, struct gf_flock *lock)
{
	gf_log (this->name, GF_LOG_CRITICAL,
		"\"features/locks\" translator is not loaded. "
		"You need to use it for proper functioning of GlusterFS");

        STACK_UNWIND_STRICT (finodelk, frame, -1, ENOSYS);
        return 0;
}


int32_t
posix_entrylk (call_frame_t *frame, xlator_t *this,
	       const char *volume, loc_t *loc, const char *basename, 
               entrylk_cmd cmd, entrylk_type type)
{
	gf_log (this->name, GF_LOG_CRITICAL,
		"\"features/locks\" translator is not loaded. "
		"You need to use it for proper functioning of GlusterFS");

        STACK_UNWIND_STRICT (entrylk, frame, -1, ENOSYS);
        return 0;
}

int32_t
posix_fentrylk (call_frame_t *frame, xlator_t *this,
		const char *volume, fd_t *fd, const char *basename, 
                entrylk_cmd cmd, entrylk_type type)
{
	gf_log (this->name, GF_LOG_CRITICAL,
		"\"features/locks\" translator is not loaded. "
		" You need to use it for proper functioning of GlusterFS");

        STACK_UNWIND_STRICT (fentrylk, frame, -1, ENOSYS);
        return 0;
}


int32_t
posix_do_readdir (call_frame_t *frame, xlator_t *this,
                  fd_t *fd, size_t size, off_t off, int whichop)
{
	uint64_t              tmp_pfd        = 0;
        struct posix_fd      *pfd            = NULL;
        DIR                  *dir            = NULL;
        int                   ret            = -1;
        size_t                filled         = 0;
	int                   count          = 0;
        int32_t               op_ret         = -1;
        int32_t               op_errno       = 0;
        gf_dirent_t          *this_entry     = NULL;
	gf_dirent_t           entries;
        struct dirent        *entry          = NULL;
        off_t                 in_case        = -1;
        int32_t               this_size      = -1;
        char                 *real_path      = NULL;
        int                   real_path_len  = -1;
        char                 *entry_path     = NULL;
        int                   entry_path_len = -1;
        struct posix_private *priv           = NULL;
        struct iatt           stbuf          = {0, };
        char                  base_path[PATH_MAX] = {0,};
        gf_dirent_t          *tmp_entry      = NULL;

        VALIDATE_OR_GOTO (frame, out);
        VALIDATE_OR_GOTO (this, out);
        VALIDATE_OR_GOTO (fd, out);

	INIT_LIST_HEAD (&entries.list);

        priv = this->private;

        ret = fd_ctx_get (fd, this, &tmp_pfd);
        if (ret < 0) {
                gf_log (this->name, GF_LOG_DEBUG,
                        "pfd is NULL, fd=%p", fd);
                op_errno = -ret;
                goto out;
        }
	pfd = (struct posix_fd *)(long)tmp_pfd;
        if (!pfd->path) {
                op_errno = EBADFD;
                gf_log (this->name, GF_LOG_DEBUG,
                        "pfd does not have path set (possibly file "
			"fd, fd=%p)", fd);
                goto out;
        }

        real_path     = pfd->path;
        real_path_len = strlen (real_path);

        entry_path_len = real_path_len + NAME_MAX;
        entry_path     = alloca (entry_path_len);

        strncpy(base_path, POSIX_BASE_PATH(this), sizeof(base_path));
        base_path[strlen(base_path)] = '/';

        if (!entry_path) {
                op_errno = errno;
                gf_log (this->name, GF_LOG_ERROR,
                        "Out of memory.");
                goto out;
        }

        strncpy (entry_path, real_path, entry_path_len);
        entry_path[real_path_len] = '/';

        dir = pfd->dir;

        if (!dir) {
                gf_log (this->name, GF_LOG_DEBUG,
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
				"telldir failed on dir=%p: %s",
                                dir, strerror (errno));
                        goto out;
                }

                errno = 0;
                entry = readdir (dir);

                if (!entry) {
                        if (errno == EBADF) {
                                op_errno = errno;
                                gf_log (this->name, GF_LOG_DEBUG,
					"readdir failed on dir=%p: %s",
                                        dir, strerror (op_errno));
                                goto out;
                        }
                        break;
                }

                if ((!strcmp(real_path, base_path))
                    && (!strcmp(entry->d_name, GF_REPLICATE_TRASH_DIR)))
                        continue;

                this_size = max (sizeof (gf_dirent_t),
                                 sizeof (gfs3_dirplist))
                        + strlen (entry->d_name) + 1;

                if (this_size + filled > size) {
                        seekdir (dir, in_case);
                        break;
                }

                stbuf.ia_ino = entry->d_ino;

                entry->d_ino = stbuf.ia_ino;

                this_entry = gf_dirent_for_name (entry->d_name);

                if (!this_entry) {
                        gf_log (this->name, GF_LOG_ERROR,
                                "could not create gf_dirent for entry %s: (%s)",
                                entry->d_name, strerror (errno));
                        goto out;
                }
                this_entry->d_off = telldir (dir);
                this_entry->d_ino = entry->d_ino;

                list_add_tail (&this_entry->list, &entries.list);

                filled += this_size;
                count ++;
        }

        if (whichop == GF_FOP_READDIRP) {
                list_for_each_entry (tmp_entry, &entries.list, list) {
                        strcpy (entry_path + real_path_len + 1, 
                                tmp_entry->d_name);
                        posix_lstat_with_gfid (this, entry_path, &stbuf);
                        tmp_entry->d_stat = stbuf;
                }
        }
        op_ret = count;
        errno = 0;
        if ((!readdir (dir) && (errno == 0)))
                op_errno = ENOENT;

 out:
        STACK_UNWIND_STRICT (readdir, frame, op_ret, op_errno, &entries);

	gf_dirent_free (&entries);

        return 0;
}


int32_t
posix_readdir (call_frame_t *frame, xlator_t *this,
               fd_t *fd, size_t size, off_t off)
{
        posix_do_readdir (frame, this, fd, size, off, GF_FOP_READDIR);
        return 0;
}


int32_t
posix_readdirp (call_frame_t *frame, xlator_t *this,
                fd_t *fd, size_t size, off_t off)
{
        posix_do_readdir (frame, this, fd, size, off, GF_FOP_READDIRP);
        return 0;
}

int32_t
posix_priv (xlator_t *this)
{
        struct posix_private *priv = NULL;
        char  key_prefix[GF_DUMP_MAX_BUF_LEN];
        char  key[GF_DUMP_MAX_BUF_LEN];

        snprintf(key_prefix, GF_DUMP_MAX_BUF_LEN, "%s.%s", this->type, 
                       this->name);
        gf_proc_dump_add_section(key_prefix);

        if (!this) 
                return 0;

        priv = this->private;

        if (!priv) 
                return 0;

        gf_proc_dump_build_key(key, key_prefix, "base_path");
        gf_proc_dump_write(key,"%s", priv->base_path);
        gf_proc_dump_build_key(key, key_prefix, "base_path_length");
        gf_proc_dump_write(key,"%d", priv->base_path_length);
        gf_proc_dump_build_key(key, key_prefix, "max_read");
        gf_proc_dump_write(key,"%d", priv->read_value);
        gf_proc_dump_build_key(key, key_prefix, "max_write");
        gf_proc_dump_write(key,"%d", priv->write_value);
        gf_proc_dump_build_key(key, key_prefix, "nr_files");
        gf_proc_dump_write(key,"%ld", priv->nr_files);

        return 0;
}

int32_t
posix_inode (xlator_t *this)
{
        return 0;
}


int32_t
posix_rchecksum (call_frame_t *frame, xlator_t *this,
                 fd_t *fd, off_t offset, int32_t len)
{
        char *buf = NULL;

        int       _fd      = -1;
        uint64_t  tmp_pfd  =  0;

        struct posix_fd *pfd  = NULL;

        int op_ret   = -1;
        int op_errno = 0;

        int ret = 0;

        int32_t weak_checksum = 0;
        uint8_t strong_checksum[MD5_DIGEST_LEN];

        VALIDATE_OR_GOTO (frame, out);
        VALIDATE_OR_GOTO (this, out);
        VALIDATE_OR_GOTO (fd, out);

        memset (strong_checksum, 0, MD5_DIGEST_LEN);
        buf = GF_CALLOC (1, len, gf_posix_mt_char);

        if (!buf) {
                op_errno = ENOMEM;
                gf_log (this->name, GF_LOG_ERROR,
                        "Out of memory");
                goto out;
        }

        ret = fd_ctx_get (fd, this, &tmp_pfd);
        if (ret < 0) {
                gf_log (this->name, GF_LOG_DEBUG,
                        "pfd is NULL, fd=%p", fd);
                op_errno = -ret;
                goto out;
        }
	pfd = (struct posix_fd *)(long) tmp_pfd;

        _fd = pfd->fd;

        ret = pread (_fd, buf, len, offset);
        if (ret < 0) {
                gf_log (this->name, GF_LOG_DEBUG,
                        "pread of %d bytes returned %d (%s)",
                        len, ret, strerror (errno));

                op_errno = errno;
                goto out;
        }

        weak_checksum = gf_rsync_weak_checksum (buf, len);
        gf_rsync_strong_checksum (buf, len, strong_checksum);

        GF_FREE (buf);

        op_ret = 0;
out:
        STACK_UNWIND_STRICT (rchecksum, frame, op_ret, op_errno,
                             weak_checksum, strong_checksum);
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

int32_t
mem_acct_init (xlator_t *this)
{
        int     ret = -1;

        if (!this)
                return ret;

        ret = xlator_mem_acct_init (this, gf_posix_mt_end + 1);
        
        if (ret != 0) {
                gf_log(this->name, GF_LOG_ERROR, "Memory accounting init"
                                "failed");
                return ret;
        }

        return ret;
}

/**
 * init -
 */
int
init (xlator_t *this)
{
        struct posix_private  *_private      = NULL;
        data_t                *dir_data      = NULL;
	data_t                *tmp_data      = NULL;
        struct stat            buf           = {0,};
	gf_boolean_t           tmp_bool      = 0;
        int                    dict_ret      = 0;
        int                    ret           = 0;
        int                    op_ret        = -1;
        int32_t                janitor_sleep = 0;

        dir_data = dict_get (this->options, "directory");

        if (this->children) {
                gf_log (this->name, GF_LOG_CRITICAL,
                        "FATAL: storage/posix cannot have subvolumes");
                ret = -1;
                goto out;
        }

	if (!this->parents) {
		gf_log (this->name, GF_LOG_WARNING,
			"Volume is dangling. Please check the volume file.");
	}

        if (!dir_data) {
                gf_log (this->name, GF_LOG_CRITICAL,
                        "Export directory not specified in volume file.");
                ret = -1;
                goto out;
        }

        umask (000); // umask `masking' is done at the client side

        /* Check whether the specified directory exists, if not log it. */
        op_ret = stat (dir_data->data, &buf);
        if ((op_ret != 0) || !S_ISDIR (buf.st_mode)) {
                gf_log (this->name, GF_LOG_ERROR,
                        "Directory '%s' doesn't exist, exiting.",
			dir_data->data);
                ret = -1;
                goto out;
        }


        /* Check for Extended attribute support, if not present, log it */
        op_ret = sys_lsetxattr (dir_data->data,
			    "trusted.glusterfs.test", "working", 8, 0);
        if (op_ret < 0) {
		tmp_data = dict_get (this->options,
				     "mandate-attribute");
		if (tmp_data) {
			if (gf_string2boolean (tmp_data->data,
					       &tmp_bool) == -1) {
				gf_log (this->name, GF_LOG_ERROR,
					"wrong option provided for key "
					"\"mandate-attribute\"");
				ret = -1;
				goto out;
			}
			if (!tmp_bool) {
				gf_log (this->name, GF_LOG_WARNING,
					"Extended attribute not supported, "
					"starting as per option");
			} else {
				gf_log (this->name, GF_LOG_CRITICAL,
					"Extended attribute not supported, "
					"exiting.");
				ret = -1;
				goto out;
			}
		} else {
			gf_log (this->name, GF_LOG_CRITICAL,
				"Extended attribute not supported, exiting.");
			ret = -1;
			goto out;
		}
        }

        _private = GF_CALLOC (1, sizeof (*_private),
                              gf_posix_mt_posix_private);
        if (!_private) {
                gf_log (this->name, GF_LOG_ERROR,
                        "Out of memory.");
                ret = -1;
                goto out;
        }

        _private->base_path = gf_strdup (dir_data->data);
        _private->base_path_length = strlen (_private->base_path);

        _private->trash_path = GF_CALLOC (1, _private->base_path_length
                                          + strlen ("/")
                                          + strlen (GF_REPLICATE_TRASH_DIR)
                                          + 1,
                                          gf_posix_mt_trash_path);

        if (!_private->trash_path) {
                gf_log (this->name, GF_LOG_ERROR,
                        "Out of memory.");
                ret = -1;
                goto out;
        }

        strncpy (_private->trash_path, _private->base_path, _private->base_path_length);
        strcat (_private->trash_path, "/" GF_REPLICATE_TRASH_DIR);

        LOCK_INIT (&_private->lock);

        ret = dict_get_str (this->options, "hostname", &_private->hostname);
        if (ret) {
                _private->hostname = GF_CALLOC (256, sizeof (char),
                                                gf_common_mt_char);
                if (!_private->hostname) {
                        gf_log (this->name, GF_LOG_ERROR, "not enough memory");
                        goto out;
                }
                ret = gethostname (_private->hostname, 256);
                if (ret < 0) {
                        gf_log (this->name, GF_LOG_WARNING,
                                "could not find hostname (%s)", strerror (errno));
                }
        }

        _private->export_statfs = 1;
        tmp_data = dict_get (this->options, "export-statfs-size");
        if (tmp_data) {
		if (gf_string2boolean (tmp_data->data,
				       &_private->export_statfs) == -1) {
			ret = -1;
			gf_log (this->name, GF_LOG_ERROR,
				"'export-statfs-size' takes only boolean "
				"options");
			goto out;
		}
                if (!_private->export_statfs)
                        gf_log (this->name, GF_LOG_DEBUG,
				"'statfs()' returns dummy size");
        }

        _private->background_unlink = 0;
        tmp_data = dict_get (this->options, "background-unlink");
        if (tmp_data) {
		if (gf_string2boolean (tmp_data->data,
				       &_private->background_unlink) == -1) {
			ret = -1;
			gf_log (this->name, GF_LOG_ERROR,
				"'background-unlink' takes only boolean "
				"options");
			goto out;
		}

                if (_private->background_unlink)
                        gf_log (this->name, GF_LOG_DEBUG,
				"unlinks will be performed in background");
        }

        tmp_data = dict_get (this->options, "o-direct");
        if (tmp_data) {
		if (gf_string2boolean (tmp_data->data,
				       &_private->o_direct) == -1) {
			ret = -1;
			gf_log (this->name, GF_LOG_ERROR,
				"wrong option provided for 'o-direct'");
			goto out;
		}
		if (_private->o_direct)
                        gf_log (this->name, GF_LOG_DEBUG,
                                "o-direct mode is enabled (O_DIRECT "
				"for every open)");
        }

        _private->janitor_sleep_duration = 600;

	dict_ret = dict_get_int32 (this->options, "janitor-sleep-duration",
                                   &janitor_sleep);
	if (dict_ret == 0) {
		gf_log (this->name, GF_LOG_DEBUG,
			"Setting janitor sleep duration to %d.",
			janitor_sleep);

		_private->janitor_sleep_duration = janitor_sleep;
	}

#ifndef GF_DARWIN_HOST_OS
        {
                struct rlimit lim;
                lim.rlim_cur = 1048576;
                lim.rlim_max = 1048576;

                if (setrlimit (RLIMIT_NOFILE, &lim) == -1) {
                        gf_log (this->name, GF_LOG_WARNING,
				"Failed to set 'ulimit -n "
				" 1048576': %s", strerror(errno));
                        lim.rlim_cur = 65536;
                        lim.rlim_max = 65536;

                        if (setrlimit (RLIMIT_NOFILE, &lim) == -1) {
                                gf_log (this->name, GF_LOG_WARNING,
					"Failed to set maximum allowed open "
                                        "file descriptors to 64k: %s", 
                                        strerror(errno));
                        }
                        else {
                                gf_log (this->name, GF_LOG_NORMAL,
					"Maximum allowed open file descriptors "
                                        "set to 65536");
                        }
                }
        }
#endif
        this->private = (void *)_private;

        pthread_mutex_init (&_private->janitor_lock, NULL);
        pthread_cond_init (&_private->janitor_cond, NULL);
        INIT_LIST_HEAD (&_private->janitor_fds);

        posix_spawn_janitor_thread (this);
 out:
        return ret;
}

void
fini (xlator_t *this)
{
        struct posix_private *priv = this->private;
        if (!priv)
                return;
        this->private = NULL;
        sys_lremovexattr (priv->base_path, "trusted.glusterfs.test");
        GF_FREE (priv);
        return;
}

struct xlator_dumpops dumpops = {
        .priv    = posix_priv,
        .inode   = posix_inode,
};

struct xlator_fops fops = {
        .lookup      = posix_lookup,
        .stat        = posix_stat,
        .opendir     = posix_opendir,
        .readdir     = posix_readdir,
        .readdirp    = posix_readdirp,
        .readlink    = posix_readlink,
        .mknod       = posix_mknod,
        .mkdir       = posix_mkdir,
        .unlink      = posix_unlink,
        .rmdir       = posix_rmdir,
        .symlink     = posix_symlink,
        .rename      = posix_rename,
        .link        = posix_link,
        .truncate    = posix_truncate,
        .create      = posix_create,
        .open        = posix_open,
        .readv       = posix_readv,
        .writev      = posix_writev,
        .statfs      = posix_statfs,
        .flush       = posix_flush,
        .fsync       = posix_fsync,
        .setxattr    = posix_setxattr,
        .fsetxattr   = posix_fsetxattr,
        .getxattr    = posix_getxattr,
        .fgetxattr   = posix_fgetxattr,
        .removexattr = posix_removexattr,
        .fsyncdir    = posix_fsyncdir,
        .access      = posix_access,
        .ftruncate   = posix_ftruncate,
        .fstat       = posix_fstat,
        .lk          = posix_lk,
	.inodelk     = posix_inodelk,
	.finodelk    = posix_finodelk,
	.entrylk     = posix_entrylk,
	.fentrylk    = posix_fentrylk,
        .rchecksum   = posix_rchecksum,
	.xattrop     = posix_xattrop,
	.fxattrop    = posix_fxattrop,
        .setattr     = posix_setattr,
        .fsetattr    = posix_fsetattr,
};

struct xlator_cbks cbks = {
	.release     = posix_release,
	.releasedir  = posix_releasedir,
	.forget      = posix_forget
};

struct volume_options options[] = {
	{ .key  = {"o-direct"},
	  .type = GF_OPTION_TYPE_BOOL },
	{ .key  = {"directory"},
	  .type = GF_OPTION_TYPE_PATH },
	{ .key  = {"hostname"},
	  .type = GF_OPTION_TYPE_ANY },
	{ .key  = {"export-statfs-size"},
	  .type = GF_OPTION_TYPE_BOOL },
	{ .key  = {"mandate-attribute"},
	  .type = GF_OPTION_TYPE_BOOL },
        { .key  = {"background-unlink"},
          .type = GF_OPTION_TYPE_BOOL },
        { .key  = {"janitor-sleep-duration"},
          .type = GF_OPTION_TYPE_INT },
	{ .key  = {NULL} }
};
