/*
   Copyright (c) 2006-2012 Red Hat, Inc. <http://www.redhat.com>
   This file is part of GlusterFS.

   This file is licensed to you under your choice of the GNU Lesser
   General Public License, version 3 or any later version (LGPLv3 or
   later), or the GNU General Public License, version 2 (GPLv2), in all
   cases as published by the Free Software Foundation.
*/
#define __XOPEN_SOURCE 500

#include <stdint.h>
#include <sys/time.h>
#include <sys/resource.h>
#include <errno.h>
#include <libgen.h>
#include <pthread.h>
#include <ftw.h>
#include <sys/stat.h>
#include <signal.h>

#ifdef HAVE_SYS_ACL_H
#ifdef HAVE_ACL_LIBACL_H /* for acl_to_any_text() */
#include <acl/libacl.h>
#else /* FreeBSD and others */
#include <sys/acl.h>
#endif
#endif

#ifndef GF_BSD_HOST_OS
#include <alloca.h>
#endif /* GF_BSD_HOST_OS */

#include <fnmatch.h>
#include "glusterfs.h"
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
#include "glusterfs-acl.h"
#include "events.h"

char *marker_xattrs[] = {"trusted.glusterfs.quota.*",
                         "trusted.glusterfs.*.xtime",
                         NULL};

char *marker_contri_key = "trusted.*.*.contri";

static char* posix_ignore_xattrs[] = {
        "gfid-req",
        GLUSTERFS_ENTRYLK_COUNT,
        GLUSTERFS_INODELK_COUNT,
        GLUSTERFS_POSIXLK_COUNT,
        GLUSTERFS_PARENT_ENTRYLK,
        GF_GFIDLESS_LOOKUP,
        GLUSTERFS_INODELK_DOM_COUNT,
        GLUSTERFS_INTERNAL_FOP_KEY,
        NULL
};

static char* list_xattr_ignore_xattrs[] = {
        GF_SELINUX_XATTR_KEY,
        GF_XATTR_VOL_ID_KEY,
        GFID_XATTR_KEY,
        NULL
};

gf_boolean_t
posix_special_xattr (char **pattern, char *key)
{
        int          i    = 0;
        gf_boolean_t flag = _gf_false;

        GF_VALIDATE_OR_GOTO ("posix", pattern, out);
        GF_VALIDATE_OR_GOTO ("posix", key, out);

        for (i = 0; pattern[i]; i++) {
                if (!fnmatch (pattern[i], key, 0)) {
                        flag = _gf_true;
                        break;
                }
        }
out:
        return flag;
}

static gf_boolean_t
_is_in_array (char **str_array, char *str)
{
        int i = 0;

        if (!str)
                return _gf_false;

        for (i = 0; str_array[i]; i++) {
                if (strcmp (str, str_array[i]) == 0)
                        return _gf_true;
        }
        return _gf_false;
}

static gf_boolean_t
posix_xattr_ignorable (char *key)
{
        return _is_in_array (posix_ignore_xattrs, key);
}

static int
_posix_xattr_get_set_from_backend (posix_xattr_filler_t *filler, char *key)
{
        ssize_t  xattr_size = -1;
        int      ret        = 0;
        char     *value     = NULL;
        char     val_buf[256] = {0};
        gf_boolean_t have_val   = _gf_false;

        if (!gf_is_valid_xattr_namespace (key)) {
                ret = -1;
                goto out;
        }

        /* Most of the gluster internal xattrs don't exceed 256 bytes. So try
         * getxattr with ~256 bytes. If it gives ERANGE then go the old way
         * of getxattr with NULL buf to find the length and then getxattr with
         * allocated buf to fill the data. This way we reduce lot of getxattrs.
         */
        if (filler->real_path)
                xattr_size = sys_lgetxattr (filler->real_path, key, val_buf,
                                            sizeof (val_buf) - 1);
        else
                xattr_size = sys_fgetxattr (filler->fdnum, key, val_buf,
                                            sizeof (val_buf) - 1);

        if (xattr_size >= 0) {
                have_val = _gf_true;
        } else if (xattr_size == -1 && errno != ERANGE) {
                ret = -1;
                goto out;
        }

        if (have_val) {
                /*No need to do getxattr*/
        } else if (filler->real_path) {
                xattr_size = sys_lgetxattr (filler->real_path, key, NULL, 0);
        } else {
                xattr_size = sys_fgetxattr (filler->fdnum, key, NULL, 0);
        }

        if (xattr_size != -1) {
                value = GF_CALLOC (1, xattr_size + 1, gf_posix_mt_char);
                if (!value)
                        goto out;

                if (have_val) {
                        memcpy (value, val_buf, xattr_size);
                } else if (filler->real_path) {
                        xattr_size = sys_lgetxattr (filler->real_path, key,
                                                    value, xattr_size);
                } else {
                        xattr_size = sys_fgetxattr (filler->fdnum, key, value,
                                                    xattr_size);
                }
                if (xattr_size == -1) {
                        if (filler->real_path)
                                gf_msg (filler->this->name, GF_LOG_WARNING, 0,
                                        P_MSG_XATTR_FAILED,
                                        "getxattr failed. path: %s, key: %s",
                                        filler->real_path, key);
                        else
                                gf_msg (filler->this->name, GF_LOG_WARNING, 0,
                                        P_MSG_XATTR_FAILED,
                                        "getxattr failed. gfid: %s, key: %s",
                                        uuid_utoa (filler->fd->inode->gfid),
                                        key);
                        GF_FREE (value);
                        goto out;
                }

                value[xattr_size] = '\0';
                ret = dict_set_bin (filler->xattr, key, value, xattr_size);
                if (ret < 0) {
                        if (filler->real_path)
                                gf_msg_debug (filler->this->name, 0,
                                        "dict set failed. path: %s, key: %s",
                                        filler->real_path, key);
                        else
                                gf_msg_debug (filler->this->name, 0,
                                        "dict set failed. gfid: %s, key: %s",
                                        uuid_utoa (filler->fd->inode->gfid),
                                        key);
                        GF_FREE (value);
                        goto out;
                }
        }
        ret = 0;
out:
        return ret;
}

static int gf_posix_xattr_enotsup_log;

static int
_posix_get_marker_all_contributions (posix_xattr_filler_t *filler)
{
        ssize_t  size = -1, remaining_size = -1, list_offset = 0;
        int      ret  = -1;
        char    *list = NULL, key[4096] = {0, };

        if (filler->real_path)
                size = sys_llistxattr (filler->real_path, NULL, 0);
        else
                size = sys_flistxattr (filler->fdnum, NULL, 0);
        if (size == -1) {
                if ((errno == ENOTSUP) || (errno == ENOSYS)) {
                        GF_LOG_OCCASIONALLY (gf_posix_xattr_enotsup_log,
                                             THIS->name, GF_LOG_WARNING,
                                             "Extended attributes not "
                                             "supported (try remounting brick"
                                             " with 'user_xattr' flag)");
                } else {
                        if (filler->real_path)
                                gf_msg (THIS->name, GF_LOG_WARNING, errno,
                                        P_MSG_XATTR_FAILED,
                                        "listxattr failed on %s",
                                        filler->real_path);
                        else
                                gf_msg (THIS->name, GF_LOG_WARNING, errno,
                                        P_MSG_XATTR_FAILED,
                                        "listxattr failed on %s",
                                        uuid_utoa (filler->fd->inode->gfid));
                }
                goto out;
        }

        if (size == 0) {
                ret = 0;
                goto out;
        }

        list = alloca (size);
        if (!list) {
                goto out;
        }

        if (filler->real_path)
                size = sys_llistxattr (filler->real_path, list, size);
        else
                size = sys_flistxattr (filler->fdnum, list, size);
        if (size <= 0) {
                ret = size;
                goto out;
        }

        remaining_size = size;
        list_offset = 0;

        while (remaining_size > 0) {
                strcpy (key, list + list_offset);
                if (fnmatch (marker_contri_key, key, 0) == 0) {
                        ret = _posix_xattr_get_set_from_backend (filler, key);
                }

                remaining_size -= strlen (key) + 1;
                list_offset += strlen (key) + 1;
        }

        ret = 0;

out:
        return ret;
}

static int
_posix_get_marker_quota_contributions (posix_xattr_filler_t *filler, char *key)
{
        char *saveptr = NULL, *token = NULL, *tmp_key = NULL;
        char *ptr     = NULL;
        int   i       = 0, ret = 0;

        tmp_key = ptr = gf_strdup (key);
        for (i = 0; i < 4; i++) {
                token = strtok_r (tmp_key, ".", &saveptr);
                tmp_key = NULL;
        }

        if (strncmp (token, "contri", strlen ("contri")) == 0) {
                ret = _posix_get_marker_all_contributions (filler);
        } else {
                ret = _posix_xattr_get_set_from_backend (filler, key);
        }

        GF_FREE (ptr);

        return ret;
}

static inode_t *
_get_filler_inode (posix_xattr_filler_t *filler)
{
        if (filler->fd)
                return filler->fd->inode;
        else if (filler->loc && filler->loc->inode)
                return filler->loc->inode;
        else
                return NULL;
}

static int
_posix_filler_get_openfd_count (posix_xattr_filler_t *filler, char *key)
{
        inode_t  *inode            = NULL;
        int      ret               = -1;

        inode = _get_filler_inode (filler);
        if (!inode || gf_uuid_is_null (inode->gfid))
                        goto out;

        ret = dict_set_uint32 (filler->xattr, key, inode->fd_count);
        if (ret < 0) {
                gf_msg (filler->this->name, GF_LOG_WARNING, 0,
                        P_MSG_DICT_SET_FAILED,
                        "Failed to set dictionary value for %s", key);
                goto out;
        }
out:
        return ret;
}

static int
_posix_xattr_get_set (dict_t *xattr_req, char *key, data_t *data,
                      void *xattrargs)
{
        posix_xattr_filler_t *filler = xattrargs;
        int       ret      = -1;
        char     *databuf  = NULL;
        int       _fd      = -1;
        loc_t    *loc      = NULL;
        ssize_t  req_size  = 0;


        if (posix_xattr_ignorable (key))
                goto out;
        /* should size be put into the data_t ? */
        if (!strcmp (key, GF_CONTENT_KEY)
            && IA_ISREG (filler->stbuf->ia_type)) {
                if (!filler->real_path)
                        goto out;

                /* file content request */
                req_size = data_to_uint64 (data);
                if (req_size >= filler->stbuf->ia_size) {
                        _fd = open (filler->real_path, O_RDONLY);
                        if (_fd == -1) {
                                gf_msg (filler->this->name, GF_LOG_ERROR, errno,
                                        P_MSG_XDATA_GETXATTR,
                                        "Opening file %s failed",
                                        filler->real_path);
                                goto err;
                        }

                        /*
                         * There could be a situation where the ia_size is
                         * zero. GF_CALLOC will return a pointer to the
                         * memory initialized by gf_mem_set_acct_info.
                         * This function adds a header and a footer to
                         * the allocated memory.  The returned pointer
                         * points to the memory just after the header, but
                         * when size is zero, there is no space for user
                         * data. The memory can be freed by calling GF_FREE.
                         */
                        databuf = GF_CALLOC (1, filler->stbuf->ia_size,
                                             gf_posix_mt_char);
                        if (!databuf) {
                                goto err;
                        }

                        ret = sys_read (_fd, databuf, filler->stbuf->ia_size);
                        if (ret == -1) {
                                gf_msg (filler->this->name, GF_LOG_ERROR, errno,
                                         P_MSG_XDATA_GETXATTR,
                                        "Read on file %s failed",
                                        filler->real_path);
                                goto err;
                        }

                        ret = sys_close (_fd);
                        _fd = -1;
                        if (ret == -1) {
                                gf_msg (filler->this->name, GF_LOG_ERROR, errno,
                                        P_MSG_XDATA_GETXATTR,
                                        "Close on file %s failed",
                                        filler->real_path);
                                goto err;
                        }

                        ret = dict_set_bin (filler->xattr, key,
                                            databuf, filler->stbuf->ia_size);
                        if (ret < 0) {
                                gf_msg (filler->this->name, GF_LOG_ERROR, 0,
                                        P_MSG_XDATA_GETXATTR,
                                        "failed to set dict value. key: %s,"
                                        "path: %s",
                                        key, filler->real_path);
                                goto err;
                        }

                        /* To avoid double free in cleanup below */
                        databuf = NULL;
                err:
                        if (_fd != -1)
                                sys_close (_fd);
                        GF_FREE (databuf);
                }
        } else if (!strcmp (key, GLUSTERFS_OPEN_FD_COUNT)) {
                ret = _posix_filler_get_openfd_count (filler, key);
                loc = filler->loc;
                if (loc) {
                        ret = dict_set_uint32 (filler->xattr, key,
                                               loc->inode->fd_count);
                        if (ret < 0)
                                gf_msg (filler->this->name, GF_LOG_WARNING, 0,
                                        P_MSG_XDATA_GETXATTR,
                                        "Failed to set dictionary value for %s",
                                        key);
                }
        } else if (!strcmp (key, GET_ANCESTRY_PATH_KEY)) {
                /* As of now, the only consumers of POSIX_ANCESTRY_PATH attempt
                 * fetching it via path-based fops. Hence, leaving it as it is
                 * for now.
                 */
                if (!filler->real_path)
                        goto out;
                char *path = NULL;
                ret = posix_get_ancestry (filler->this, filler->loc->inode,
                                          NULL, &path, POSIX_ANCESTRY_PATH,
                                          &filler->op_errno, xattr_req);
                if (ret < 0) {
                        goto out;
                }

                ret = dict_set_dynstr (filler->xattr, GET_ANCESTRY_PATH_KEY,
                                       path);
                if (ret < 0) {
                        GF_FREE (path);
                        goto out;
                }

        } else if (fnmatch (marker_contri_key, key, 0) == 0) {
                ret = _posix_get_marker_quota_contributions (filler, key);
        } else if (strcmp(key, GF_REQUEST_LINK_COUNT_XDATA) == 0) {
                ret = dict_set (filler->xattr,
                                GF_REQUEST_LINK_COUNT_XDATA, data);
        } else {
                ret = _posix_xattr_get_set_from_backend (filler, key);
        }
out:
        return 0;
}


int
posix_fill_gfid_path (xlator_t *this, const char *path, struct iatt *iatt)
{
        int ret = 0;
        ssize_t size = 0;

        if (!iatt)
                return 0;

        size = sys_lgetxattr (path, GFID_XATTR_KEY, iatt->ia_gfid, 16);
        /* Return value of getxattr */
        if ((size == 16) || (size == -1))
                ret = 0;
        else
                ret = size;

        return ret;
}


int
posix_fill_gfid_fd (xlator_t *this, int fd, struct iatt *iatt)
{
        int ret = 0;
        ssize_t size = 0;

        if (!iatt)
                return 0;

        size = sys_fgetxattr (fd, GFID_XATTR_KEY, iatt->ia_gfid, 16);
        /* Return value of getxattr */
        if ((size == 16) || (size == -1))
                ret = 0;
        else
                ret = size;

        return ret;
}

void
posix_fill_ino_from_gfid (xlator_t *this, struct iatt *buf)
{
        /* consider least significant 8 bytes of value out of gfid */
        if (gf_uuid_is_null (buf->ia_gfid)) {
                buf->ia_ino = -1;
                goto out;
        }
        buf->ia_ino = gfid_to_ino (buf->ia_gfid);
out:
        return;
}

int
posix_fdstat (xlator_t *this, int fd, struct iatt *stbuf_p)
{
        int                    ret     = 0;
        struct stat            fstatbuf = {0, };
        struct iatt            stbuf = {0, };

        ret = sys_fstat (fd, &fstatbuf);
        if (ret == -1)
                goto out;

        if (fstatbuf.st_nlink && !S_ISDIR (fstatbuf.st_mode))
                fstatbuf.st_nlink--;

        iatt_from_stat (&stbuf, &fstatbuf);

        ret = posix_fill_gfid_fd (this, fd, &stbuf);

        posix_fill_ino_from_gfid (this, &stbuf);

        if (stbuf_p)
                *stbuf_p = stbuf;

out:
        return ret;
}


int
posix_istat (xlator_t *this, uuid_t gfid, const char *basename,
             struct iatt *buf_p)
{
        char        *real_path = NULL;
        struct stat  lstatbuf = {0, };
        struct iatt  stbuf = {0, };
        int          ret = 0;
        struct posix_private *priv = NULL;

        priv = this->private;

        MAKE_HANDLE_PATH (real_path, this, gfid, basename);
        if (!real_path) {
                gf_msg (this->name, GF_LOG_ERROR, ESTALE,
                        P_MSG_HANDLE_PATH_CREATE,
                        "Failed to create handle path for %s/%s",
                        uuid_utoa (gfid), basename ? basename : "");
                errno = ESTALE;
                ret = -1;
                goto out;
        }

        ret = sys_lstat (real_path, &lstatbuf);

        if (ret != 0) {
                if (ret == -1) {
                        if (errno != ENOENT && errno != ELOOP)
                                gf_msg (this->name, GF_LOG_WARNING, errno,
                                        P_MSG_LSTAT_FAILED,
                                        "lstat failed on %s",
                                        real_path);
                } else {
                        // may be some backend filesystem issue
                        gf_msg (this->name, GF_LOG_ERROR, 0, P_MSG_LSTAT_FAILED,
                                "lstat failed on %s and return value is %d "
                                "instead of -1. Please see dmesg output to "
                                "check whether the failure is due to backend "
                                "filesystem issue", real_path, ret);
                        ret = -1;
                }
                goto out;
        }

        if ((lstatbuf.st_ino == priv->handledir.st_ino) &&
            (lstatbuf.st_dev == priv->handledir.st_dev)) {
                errno = ENOENT;
                return -1;
        }

        if (!S_ISDIR (lstatbuf.st_mode))
                lstatbuf.st_nlink --;

        iatt_from_stat (&stbuf, &lstatbuf);

        if (basename)
                posix_fill_gfid_path (this, real_path, &stbuf);
        else
                gf_uuid_copy (stbuf.ia_gfid, gfid);

        posix_fill_ino_from_gfid (this, &stbuf);

        if (buf_p)
                *buf_p = stbuf;
out:
        return ret;
}



int
posix_pstat (xlator_t *this, uuid_t gfid, const char *path,
             struct iatt *buf_p)
{
        struct stat  lstatbuf = {0, };
        struct iatt  stbuf = {0, };
        int          ret = 0;
        struct posix_private *priv = NULL;


        priv = this->private;

        if (gfid && !gf_uuid_is_null (gfid))
                gf_uuid_copy (stbuf.ia_gfid, gfid);
        else
                posix_fill_gfid_path (this, path, &stbuf);

        ret = sys_lstat (path, &lstatbuf);

        if (ret != 0) {
                if (ret == -1) {
                        if (errno != ENOENT)
                                gf_msg (this->name, GF_LOG_WARNING, errno,
                                        P_MSG_LSTAT_FAILED,
                                        "lstat failed on %s",
                                        path);
                } else {
                        // may be some backend filesytem issue
                        gf_msg (this->name, GF_LOG_ERROR, 0, P_MSG_LSTAT_FAILED,
                                "lstat failed on %s and return value is %d "
                                "instead of -1. Please see dmesg output to "
                                "check whether the failure is due to backend "
                                "filesystem issue", path, ret);
                        ret = -1;
                }
                goto out;
        }

        if ((lstatbuf.st_ino == priv->handledir.st_ino) &&
            (lstatbuf.st_dev == priv->handledir.st_dev)) {
                errno = ENOENT;
                return -1;
        }

        if (!S_ISDIR (lstatbuf.st_mode))
                lstatbuf.st_nlink --;

        iatt_from_stat (&stbuf, &lstatbuf);

        posix_fill_ino_from_gfid (this, &stbuf);

        if (buf_p)
                *buf_p = stbuf;
out:
        return ret;
}

static void
_handle_list_xattr (dict_t *xattr_req, const char *real_path, int fdnum,
                    posix_xattr_filler_t *filler)
{
        ssize_t               size                  = 0;
        char                 *list                  = NULL;
        int32_t               list_offset           = 0;
        ssize_t               remaining_size        = 0;
        char                  *key                  = NULL;

        if ((!real_path) && (fdnum < 0))
                goto out;

        if (real_path)
                size = sys_llistxattr (real_path, NULL, 0);
        else
                size = sys_flistxattr (fdnum, NULL, 0);

        if (size <= 0)
                goto out;

        list = alloca (size);
        if (!list)
                goto out;

        if (real_path)
                remaining_size = sys_llistxattr (real_path, list, size);
        else
                remaining_size = sys_flistxattr (fdnum, list, size);

        if (remaining_size <= 0)
                goto out;

        list_offset = 0;
        while (remaining_size > 0) {
                key = list + list_offset;

                if (_is_in_array (list_xattr_ignore_xattrs, key))
                        goto next;

                if (posix_special_xattr (marker_xattrs, key))
                        goto next;

                if (!fnmatch (GF_XATTR_STIME_PATTERN, key, 0))
                        goto next;

                if (dict_get (filler->xattr, key))
                        goto next;

                (void) _posix_xattr_get_set_from_backend (filler, key);
next:
                remaining_size -= strlen (key) + 1;
                list_offset += strlen (key) + 1;

        } /* while (remaining_size > 0) */
out:
        return;
}

dict_t *
posix_xattr_fill (xlator_t *this, const char *real_path, loc_t *loc, fd_t *fd,
                  int fdnum, dict_t *xattr_req, struct iatt *buf)
{
        dict_t     *xattr             = NULL;
        posix_xattr_filler_t filler   = {0, };
        gf_boolean_t    list          = _gf_false;

        if (dict_get (xattr_req, "list-xattr")) {
                dict_del (xattr_req, "list-xattr");
                list = _gf_true;
        }

        xattr = dict_new ();
        if (!xattr) {
                goto out;
        }

        filler.this      = this;
        filler.real_path = real_path;
        filler.xattr     = xattr;
        filler.stbuf     = buf;
        filler.loc       = loc;
        filler.fd        = fd;
        filler.fdnum    = fdnum;

        dict_foreach (xattr_req, _posix_xattr_get_set, &filler);
        if (list)
                _handle_list_xattr (xattr_req, real_path, fdnum, &filler);

out:
        return xattr;
}

void
posix_gfid_unset (xlator_t *this, dict_t *xdata)
{
        uuid_t uuid = {0, };
        int    ret  = 0;

        if (xdata == NULL)
                goto out;

        ret = dict_get_ptr (xdata, "gfid-req", (void **)&uuid);
        if (ret) {
                goto out;
        }

        posix_handle_unset (this, uuid, NULL);
out:
        return;
}

int
posix_gfid_set (xlator_t *this, const char *path, loc_t *loc, dict_t *xattr_req)
{
        void        *uuid_req = NULL;
        uuid_t       uuid_curr;
        int          ret = 0;
        ssize_t      size = 0;
        struct stat  stat = {0, };


        if (!xattr_req)
                goto out;

        if (sys_lstat (path, &stat) != 0)
                goto out;

        size = sys_lgetxattr (path, GFID_XATTR_KEY, uuid_curr, 16);
        if (size == 16) {
                ret = 0;
                goto verify_handle;
        }

        ret = dict_get_ptr (xattr_req, "gfid-req", &uuid_req);
        if (ret) {
                gf_msg_debug (this->name, 0,
                        "failed to get the gfid from dict for %s",
                        loc->path);
                goto out;
        }

        ret = sys_lsetxattr (path, GFID_XATTR_KEY, uuid_req, 16, XATTR_CREATE);
        if (ret == -1) {
                gf_msg (this->name, GF_LOG_WARNING, errno, P_MSG_GFID_FAILED,
                        "setting GFID on %s failed ", path);
                goto out;
        }
        gf_uuid_copy (uuid_curr, uuid_req);

verify_handle:
        if (!S_ISDIR (stat.st_mode))
                ret = posix_handle_hard (this, path, uuid_curr, &stat);
        else
                ret = posix_handle_soft (this, path, loc, uuid_curr, &stat);

out:
        return ret;
}


int
posix_set_file_contents (xlator_t *this, const char *path, char *keyp,
                         data_t *value, int flags)
{
        char *      key                        = NULL;
        char        real_path[PATH_MAX];
        int32_t     file_fd                    = -1;
        int         op_ret                     = 0;
        int         ret                        = -1;


        /* XXX: does not handle assigning GFID to created files */
        return -1;

        key = &(keyp[15]);
        sprintf (real_path, "%s/%s", path, key);

        if (flags & XATTR_REPLACE) {
                /* if file exists, replace it
                 * else, error out */
                file_fd = open (real_path, O_TRUNC|O_WRONLY);

                if (file_fd == -1) {
                        goto create;
                }

                if (value->len) {
                        ret = sys_write (file_fd, value->data, value->len);
                        if (ret == -1) {
                                op_ret = -errno;
                                gf_msg (this->name, GF_LOG_ERROR, errno,
                                        P_MSG_SET_FILE_CONTENTS, "write failed"
                                        "while doing setxattr for key %s on"
                                        "path%s", key, real_path);
                                goto out;
                        }

                        ret = sys_close (file_fd);
                        if (ret == -1) {
                                op_ret = -errno;
                                gf_msg (this->name, GF_LOG_ERROR, errno,
                                        P_MSG_SET_FILE_CONTENTS,
                                        "close failed on %s",
                                        real_path);
                                goto out;
                        }
                }

        create: /* we know file doesn't exist, create it */

                file_fd = open (real_path, O_CREAT|O_WRONLY, 0644);

                if (file_fd == -1) {
                        op_ret = -errno;
                        gf_msg (this->name, GF_LOG_ERROR, errno,
                                P_MSG_SET_FILE_CONTENTS, "failed to open file"
                                "%s with O_CREAT", key);
                        goto out;
                }

                ret = sys_write (file_fd, value->data, value->len);
                if (ret == -1) {
                        op_ret = -errno;
                        gf_msg (this->name, GF_LOG_ERROR, errno,
                                P_MSG_SET_FILE_CONTENTS, "write failed on %s"
                                "while setxattr with key %s", real_path, key);
                        goto out;
                }

                ret = sys_close (file_fd);
                if (ret == -1) {
                        op_ret = -errno;
                        gf_msg (this->name, GF_LOG_ERROR, errno,
                                P_MSG_SET_FILE_CONTENTS, "close failed on"
                                " %s while setxattr with key %s",
                                real_path, key);
                        goto out;
                }
        }

out:
        return op_ret;
}


int
posix_get_file_contents (xlator_t *this, uuid_t pargfid,
                         const char *name, char **contents)
{
        char        *real_path                 = NULL;
        int32_t     file_fd                    = -1;
        struct iatt stbuf                      = {0,};
        int         op_ret                     = 0;
        int         ret                        = -1;


        MAKE_HANDLE_PATH (real_path, this, pargfid, name);
        if (!real_path) {
                op_ret = -ESTALE;
                gf_msg (this->name, GF_LOG_ERROR, ESTALE,
                        P_MSG_XDATA_GETXATTR,
                        "Failed to create handle path for %s/%s",
                        uuid_utoa (pargfid), name);
                goto out;
        }

        op_ret = posix_istat (this, pargfid, name, &stbuf);
        if (op_ret == -1) {
                op_ret = -errno;
                gf_msg (this->name, GF_LOG_ERROR, errno, P_MSG_XDATA_GETXATTR,
                        "lstat failed on %s", real_path);
                goto out;
        }

        file_fd = open (real_path, O_RDONLY);

        if (file_fd == -1) {
                op_ret = -errno;
                gf_msg (this->name, GF_LOG_ERROR, errno, P_MSG_XDATA_GETXATTR,
                        "open failed on %s", real_path);
                goto out;
        }

        *contents = GF_CALLOC (stbuf.ia_size + 1, sizeof(char),
                               gf_posix_mt_char);
        if (! *contents) {
                op_ret = -errno;
                goto out;
        }

        ret = sys_read (file_fd, *contents, stbuf.ia_size);
        if (ret <= 0) {
                op_ret = -1;
                gf_msg (this->name, GF_LOG_ERROR, errno, P_MSG_XDATA_GETXATTR,
                        "read on %s failed", real_path);
                goto out;
        }

        *contents[stbuf.ia_size] = '\0';

        op_ret = sys_close (file_fd);
        file_fd = -1;
        if (op_ret == -1) {
                op_ret = -errno;
                gf_msg (this->name, GF_LOG_ERROR, errno, P_MSG_XDATA_GETXATTR,
                        "close on %s failed", real_path);
                goto out;
        }

out:
        if (op_ret < 0) {
                GF_FREE (*contents);
                if (file_fd != -1)
                        sys_close (file_fd);
        }

        return op_ret;
}

#ifdef HAVE_SYS_ACL_H
int
posix_pacl_set (const char *path, const char *key, const char *acl_s)
{
        int ret = -1;
        acl_t acl = NULL;
        acl_type_t type = 0;

        type = gf_posix_acl_get_type (key);

        acl = acl_from_text (acl_s);
        ret = acl_set_file (path, type, acl);
        if (ret)
                /* posix_handle_pair expects ret to be the errno */
                ret = -errno;

        acl_free (acl);

        return ret;
}

int
posix_pacl_get (const char *path, const char *key, char **acl_s)
{
        int ret = -1;
        acl_t acl = NULL;
        acl_type_t type = 0;
        char *acl_tmp = NULL;

        type = gf_posix_acl_get_type (key);
        if (!type)
                return -1;

        acl = acl_get_file (path, type);
        if (!acl)
                return -1;

#ifdef HAVE_ACL_LIBACL_H
        acl_tmp = acl_to_any_text (acl, NULL, ',',
                                   TEXT_ABBREVIATE | TEXT_NUMERIC_IDS);
#else /* FreeBSD and the like */
        acl_tmp = acl_to_text_np (acl, NULL, ACL_TEXT_NUMERIC_IDS);
#endif
        if (!acl_tmp)
                goto free_acl;

        *acl_s = gf_strdup (acl_tmp);
        if (*acl_s)
                ret = 0;

        acl_free (acl_tmp);
free_acl:
        acl_free (acl);

        return ret;
}
#else /* !HAVE_SYS_ACL_H (NetBSD) */
int
posix_pacl_set (const char *path, const char *key, const char *acl_s)
{
        errno = ENOTSUP;
        return -1;
}

int
posix_pacl_get (const char *path, const char *key, char **acl_s)
{
        errno = ENOTSUP;
        return -1;
}
#endif


#ifdef GF_DARWIN_HOST_OS
static
void posix_dump_buffer (xlator_t *this, const char *real_path, const char *key,
                        data_t *value, int flags)
{
        char buffer[3*value->len+1];
        int index = 0;
        buffer[0] = 0;
        gf_loglevel_t log_level = gf_log_get_loglevel ();
        if (log_level == GF_LOG_TRACE) {
                char *data = (char *) value->data;
                for (index = 0; index < value->len; index++)
                        sprintf(buffer+3*index, " %02x", data[index]);
        }
        gf_msg_debug (this->name, 0,
                "Dump %s: key:%s flags: %u length:%u data:%s ",
                real_path, key, flags, value->len,
                (log_level == GF_LOG_TRACE ? buffer : "<skipped in DEBUG>"));
}
#endif

int
posix_handle_pair (xlator_t *this, const char *real_path,
                   char *key, data_t *value, int flags, struct iatt *stbuf)
{
        int sys_ret = -1;
        int ret     = 0;

        if (XATTR_IS_PATHINFO (key)) {
                ret = -EACCES;
                goto out;
        } else if (ZR_FILE_CONTENT_REQUEST(key)) {
                ret = posix_set_file_contents (this, real_path, key, value,
                                               flags);
        } else if (GF_POSIX_ACL_REQUEST (key)) {
                if (stbuf && IS_DHT_LINKFILE_MODE (stbuf))
                        goto out;
                ret = posix_pacl_set (real_path, key, value->data);
        } else if (!strncmp(key, POSIX_ACL_ACCESS_XATTR, strlen(key))
                   && stbuf && IS_DHT_LINKFILE_MODE (stbuf)) {
                goto out;
        } else {
                sys_ret = sys_lsetxattr (real_path, key, value->data,
                                         value->len, flags);
#ifdef GF_DARWIN_HOST_OS
                posix_dump_buffer(this, real_path, key, value, flags);
#endif
                if (sys_ret < 0) {
                        ret = -errno;
                        if (errno == ENOENT) {
                                if (!posix_special_xattr (marker_xattrs,
                                                          key)) {
                                        gf_msg (this->name, GF_LOG_ERROR, errno,
                                                P_MSG_XATTR_FAILED,
                                                "setxattr on %s failed",
                                                real_path);
                                }
                        } else {

#ifdef GF_DARWIN_HOST_OS
                                if (errno == EINVAL) {
                                        gf_msg_debug (this->name, 0, "%s: key:"
                                                      "%s flags: %u length:%d "
                                                      "error:%s", real_path,
                                                      key, flags, value->len,
                                                      strerror (errno));
                                } else {
                                        gf_msg (this->name, GF_LOG_ERROR,
                                                errno, P_MSG_XATTR_FAILED,
                                                "%s: key:%s flags: "
                                                "%u length:%d",
                                                real_path, key, flags,
                                                value->len);

#else /* ! DARWIN */
                                if (errno == EEXIST)
                                        gf_msg_debug (this->name, 0,
                                                      "%s: key:%s"
                                                      "flags: %u length:%d",
                                                      real_path, key, flags,
                                                      value->len);
                                else
                                        gf_msg (this->name, GF_LOG_ERROR, errno,
                                                P_MSG_XATTR_FAILED, "%s: key:%s"
                                                "flags: %u length:%d",
                                                real_path, key, flags,
                                                value->len);
#endif /* DARWIN */
                                }

                        goto out;
                }
        }
out:
        return ret;
}

int
posix_fhandle_pair (xlator_t *this, int fd,
                    char *key, data_t *value, int flags, struct iatt *stbuf)
{
        int sys_ret = -1;
        int ret     = 0;

        if (XATTR_IS_PATHINFO (key)) {
                ret = -EACCES;
                goto out;
        } else if (!strncmp(key, POSIX_ACL_ACCESS_XATTR, strlen(key))
                   && stbuf && IS_DHT_LINKFILE_MODE (stbuf)) {
                goto out;
        }

        sys_ret = sys_fsetxattr (fd, key, value->data,
                                 value->len, flags);

        if (sys_ret < 0) {
                ret = -errno;
                if (errno == ENOENT) {
                        gf_msg (this->name, GF_LOG_ERROR, errno,
                                P_MSG_XATTR_FAILED, "fsetxattr on fd=%d"
                                " failed", fd);
                } else {

#ifdef GF_DARWIN_HOST_OS
                        if (errno == EINVAL) {
                                gf_msg_debug (this->name, 0, "fd=%d: key:%s "
                                              "error:%s", fd, key,
                                              strerror (errno));
                        } else {
                                gf_msg (this->name, GF_LOG_ERROR, errno,
                                        P_MSG_XATTR_FAILED, "fd=%d: key:%s",
                                        fd, key);
                        }

#else /* ! DARWIN */
                        gf_msg (this->name, GF_LOG_ERROR, errno,
                                P_MSG_XATTR_FAILED, "fd=%d: key:%s",
                                fd, key);
#endif /* DARWIN */
                }

                goto out;
        }

out:
        return ret;
}

static void
del_stale_dir_handle (xlator_t *this, uuid_t gfid)
{
        char    newpath[PATH_MAX] = {0, };
        uuid_t       gfid_curr = {0, };
        ssize_t      size = -1;
        gf_boolean_t stale = _gf_false;
        char         *hpath = NULL;
        struct stat  stbuf = {0, };
        struct iatt  iabuf = {0, };

        MAKE_HANDLE_GFID_PATH (hpath, this, gfid, NULL);

        /* check that it is valid directory handle */
        size = sys_lstat (hpath, &stbuf);
        if (size < 0) {
                gf_msg_debug (this->name, 0, "%s: Handle stat failed: "
                        "%s", hpath, strerror (errno));
                goto out;
        }

        iatt_from_stat (&iabuf, &stbuf);
        if (iabuf.ia_nlink != 1 || !IA_ISLNK (iabuf.ia_type)) {
                gf_msg_debug (this->name, 0, "%s: Handle nlink %d %d",
                        hpath, iabuf.ia_nlink, IA_ISLNK (iabuf.ia_type));
                goto out;
        }

        size = posix_handle_path (this, gfid, NULL, newpath, sizeof (newpath));
        if (size <= 0) {
                if (errno == ENOENT) {
                        gf_msg_debug (this->name, 0, "%s: %s", newpath,
                                strerror (ENOENT));
                        stale = _gf_true;
                }
                goto out;
        }

        size = sys_lgetxattr (newpath, GFID_XATTR_KEY, gfid_curr, 16);
        if (size < 0 && errno == ENOENT) {
                gf_msg_debug (this->name, 0, "%s: %s", newpath,
                        strerror (ENOENT));
                stale = _gf_true;
        } else if (size == 16 && gf_uuid_compare (gfid, gfid_curr)) {
                gf_msg_debug (this->name, 0, "%s: mismatching gfid: %s, "
                              "at %s", hpath, uuid_utoa (gfid_curr), newpath);
                stale = _gf_true;
        }

out:
        if (stale) {
                size = sys_unlink (hpath);
                if (size < 0 && errno != ENOENT)
                        gf_msg (this->name, GF_LOG_ERROR, errno,
                                P_MSG_STALE_HANDLE_REMOVE_FAILED, "%s: Failed"
                                "to remove handle to %s", hpath, newpath);
        } else if (size == 16) {
                gf_msg_debug (this->name, 0, "%s: Fresh handle for "
                        "%s with gfid %s", hpath, newpath,
                        uuid_utoa (gfid_curr));
        }
        return;
}

static int
janitor_walker (const char *fpath, const struct stat *sb,
                int typeflag, struct FTW *ftwbuf)
{
        struct iatt  stbuf        = {0, };
        xlator_t     *this = NULL;

        this = THIS;
        posix_pstat (this, NULL, fpath, &stbuf);
        switch (sb->st_mode & S_IFMT) {
        case S_IFREG:
        case S_IFBLK:
        case S_IFLNK:
        case S_IFCHR:
        case S_IFIFO:
        case S_IFSOCK:
                gf_msg_trace (THIS->name, 0,
                        "unlinking %s", fpath);
                sys_unlink (fpath);
                if (stbuf.ia_nlink == 1)
                        posix_handle_unset (this, stbuf.ia_gfid, NULL);
                break;

        case S_IFDIR:
                if (ftwbuf->level) { /* don't remove top level dir */
                        gf_msg_debug (THIS->name, 0,
                                "removing directory %s", fpath);

                        sys_rmdir (fpath);
                        del_stale_dir_handle (this, stbuf.ia_gfid);
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
                        gf_msg_trace (this->name, 0,
                                      "janitor cleaning out %s",
                                      priv->trash_path);

                        nftw (priv->trash_path,
                              janitor_walker,
                              32,
                              FTW_DEPTH | FTW_PHYS);

                        priv->last_landfill_check = now;
                }

                pfd = janitor_get_next_fd (this);
                if (pfd) {
                        if (pfd->dir == NULL) {
                                gf_msg_trace (this->name, 0,
                                        "janitor: closing file fd=%d", pfd->fd);
                                sys_close (pfd->fd);
                        } else {
                                gf_msg_debug (this->name, 0, "janitor: closing"
                                              " dir fd=%p", pfd->dir);
                                sys_closedir (pfd->dir);
                        }

                        GF_FREE (pfd);
                }
        }

        return NULL;
}


void
posix_spawn_janitor_thread (xlator_t *this)
{
        struct posix_private *priv = NULL;
        int ret = 0;

        priv = this->private;

        LOCK (&priv->lock);
        {
                if (!priv->janitor_present) {
                        ret = gf_thread_create (&priv->janitor, NULL,
                                                posix_janitor_thread_proc, this);

                        if (ret < 0) {
                                gf_msg (this->name, GF_LOG_ERROR, errno,
                                        P_MSG_THREAD_FAILED, "spawning janitor "
                                        "thread failed");
                                goto unlock;
                        }

                        priv->janitor_present = _gf_true;
                }
        }
unlock:
        UNLOCK (&priv->lock);
}

static int
is_fresh_file (struct stat *stat)
{
        struct timeval tv;

        gettimeofday (&tv, NULL);

        if ((stat->st_ctime >= (tv.tv_sec - 1))
            && (stat->st_ctime <= tv.tv_sec))
                return 1;

        return 0;
}


int
posix_gfid_heal (xlator_t *this, const char *path, loc_t *loc, dict_t *xattr_req)
{
        /* The purpose of this function is to prevent a race
           where an inode creation FOP (like mkdir/mknod/create etc)
           races with lookup in the following way:

                   {create thread}       |    {lookup thread}
                                         |
                                         t0
                      mkdir ("name")     |
                                         t1
                                         |     posix_gfid_set ("name", 2);
                                         t2
             posix_gfid_set ("name", 1); |
                                         t3
                      lstat ("name");    |     lstat ("name");

          In the above case mkdir FOP would have resulted with GFID 2 while
          it should have been GFID 1. It matters in the case where GFID would
          have gotten set to 1 on other subvolumes of replciate/distribute

          The "solution" here is that, if we detect lookup is attempting to
          set a GFID on a file which is created very recently, but does not
          yet have a GFID (i.e, between t1 and t2), then "fake" it as though
          posix_gfid_heal was called at t0 instead.
        */

        uuid_t       uuid_curr;
        int          ret = 0;
        struct stat  stat = {0, };

        if (!xattr_req)
                goto out;

        if (sys_lstat (path, &stat) != 0)
                goto out;

        ret = sys_lgetxattr (path, GFID_XATTR_KEY, uuid_curr, 16);
        if (ret != 16) {
                if (is_fresh_file (&stat)) {
                        ret = -1;
                        errno = ENOENT;
                        goto out;
                }
        }

        ret = posix_gfid_set (this, path, loc, xattr_req);
out:
        return ret;
}


int
posix_acl_xattr_set (xlator_t *this, const char *path, dict_t *xattr_req)
{
        int          ret = 0;
        data_t      *data = NULL;
        struct stat  stat = {0, };

        if (!xattr_req)
                goto out;

        if (sys_lstat (path, &stat) != 0)
                goto out;

        data = dict_get (xattr_req, POSIX_ACL_ACCESS_XATTR);
        if (data) {
                ret = sys_lsetxattr (path, POSIX_ACL_ACCESS_XATTR,
                                     data->data, data->len, 0);
#ifdef __FreeBSD__
                if (ret != -1) {
                        ret = 0;
                }
#endif /* __FreeBSD__ */
                if (ret != 0)
                        goto out;
        }

        data = dict_get (xattr_req, POSIX_ACL_DEFAULT_XATTR);
        if (data) {
                ret = sys_lsetxattr (path, POSIX_ACL_DEFAULT_XATTR,
                                     data->data, data->len, 0);
#ifdef __FreeBSD__
                if (ret != -1) {
                        ret = 0;
                }
#endif /* __FreeBSD__ */
                if (ret != 0)
                        goto out;
        }

out:
        return ret;
}

static int
_handle_entry_create_keyvalue_pair (dict_t *d, char *k, data_t *v,
                                    void *tmp)
{
        int                   ret = -1;
        posix_xattr_filler_t *filler = NULL;

        filler = tmp;

        if (!strcmp (GFID_XATTR_KEY, k) ||
            !strcmp ("gfid-req", k) ||
            !strcmp (POSIX_ACL_DEFAULT_XATTR, k) ||
            !strcmp (POSIX_ACL_ACCESS_XATTR, k) ||
            posix_xattr_ignorable (k) ||
            ZR_FILE_CONTENT_REQUEST(k)) {
                return 0;
        }

        ret = posix_handle_pair (filler->this, filler->real_path, k, v,
                                 XATTR_CREATE, filler->stbuf);
        if (ret < 0) {
                errno = -ret;
                return -1;
        }
        return 0;
}

int
posix_entry_create_xattr_set (xlator_t *this, const char *path,
                             dict_t *dict)
{
        int ret = -1;

        posix_xattr_filler_t filler = {0,};

        if (!dict)
                goto out;

        filler.this = this;
        filler.real_path = path;
        filler.stbuf = NULL;

        ret = dict_foreach (dict, _handle_entry_create_keyvalue_pair, &filler);

out:
        return ret;
}

static int
__posix_fd_ctx_get (fd_t *fd, xlator_t *this, struct posix_fd **pfd_p,
                    int *op_errno_p)
{
        uint64_t          tmp_pfd = 0;
        struct posix_fd  *pfd = NULL;
        int               ret = -1;
        char             *real_path = NULL;
        char             *unlink_path = NULL;
        int               _fd = -1;
        int               op_errno = 0;
        DIR              *dir = NULL;

        struct posix_private    *priv      = NULL;

        priv = this->private;

        ret = __fd_ctx_get (fd, this, &tmp_pfd);
        if (ret == 0) {
                pfd = (void *)(long) tmp_pfd;
                goto out;
        }
        if (!fd_is_anonymous(fd)) {
                gf_msg (this->name, GF_LOG_ERROR, 0,
                        P_MSG_READ_FAILED,
                        "Failed to get fd context for a non-anonymous fd, "
                        "file: %s, gfid: %s", real_path,
                        uuid_utoa (fd->inode->gfid));
                op_errno = EINVAL;
                goto out;
        }

        MAKE_HANDLE_PATH (real_path, this, fd->inode->gfid, NULL);
        if (!real_path) {
                gf_msg (this->name, GF_LOG_ERROR, 0,
                        P_MSG_READ_FAILED,
                        "Failed to create handle path (%s)",
                        uuid_utoa (fd->inode->gfid));
                ret = -1;
                op_errno = EINVAL;
                goto out;
        }
        pfd = GF_CALLOC (1, sizeof (*pfd), gf_posix_mt_posix_fd);
        if (!pfd) {
                op_errno = ENOMEM;
                goto out;
        }
        pfd->fd = -1;

        if (fd->inode->ia_type == IA_IFDIR) {
                dir = sys_opendir (real_path);
                if (!dir) {
                        op_errno = errno;
                        GF_FREE (pfd);
                        pfd = NULL;
                        goto out;
                }
                _fd = dirfd (dir);
        }

        /* Using fd->flags in case we choose to have anonymous
         * fds with different flags some day. As of today it
         * would be GF_ANON_FD_FLAGS and nothing else.
         */
        if (fd->inode->ia_type == IA_IFREG) {
                _fd = open (real_path, fd->flags);
                if ((_fd == -1) && (errno == ENOENT)) {
                        POSIX_GET_FILE_UNLINK_PATH (priv->base_path,
                                                    fd->inode->gfid,
                                                    unlink_path);
                        _fd = open (unlink_path, fd->flags);
                }
                if (_fd == -1) {
                        op_errno = errno;
                        gf_msg (this->name, GF_LOG_ERROR, op_errno,
                                P_MSG_READ_FAILED,
                                "Failed to get anonymous "
                                "real_path: %s _fd = %d", real_path, _fd);
                        GF_FREE (pfd);
                        pfd = NULL;
                        goto out;
                }
        }

        pfd->fd = _fd;
        pfd->dir = dir;
        pfd->flags = fd->flags;

        ret = __fd_ctx_set (fd, this, (uint64_t) (long) pfd);
        if (ret != 0) {
                op_errno = ENOMEM;
                if (_fd != -1)
                        sys_close (_fd);
                if (dir)
                        sys_closedir (dir);
                GF_FREE (pfd);
                pfd = NULL;
                goto out;
        }

        ret = 0;
out:
        if (ret < 0 && op_errno_p)
                *op_errno_p = op_errno;

        if (pfd_p)
                *pfd_p = pfd;
        return ret;
}


int
posix_fd_ctx_get (fd_t *fd, xlator_t *this, struct posix_fd **pfd,
                  int *op_errno)
{
        int   ret;

        LOCK (&fd->inode->lock);
        {
                ret = __posix_fd_ctx_get (fd, this, pfd, op_errno);
        }
        UNLOCK (&fd->inode->lock);

        return ret;
}

int
posix_fs_health_check (xlator_t *this)
{
        struct  posix_private *priv     = NULL;
        int     ret                     = -1;
        char    *subvol_path            = NULL;
        char    timestamp[256]          = {0,};
        int     fd                      = -1;
        int     timelen                 = -1;
        int     nofbytes                = 0;
        time_t  time_sec                = {0,};
        char    buff[64]                = {0};
        char    file_path[PATH_MAX]     = {0};
        char    *op                     = NULL;
        int     op_errno                = 0;

        GF_VALIDATE_OR_GOTO (this->name, this, out);
        priv = this->private;
        GF_VALIDATE_OR_GOTO ("posix-helpers", priv, out);

        subvol_path = priv->base_path;
        snprintf (file_path, sizeof (file_path), "%s/%s/health_check",
                  subvol_path, GF_HIDDEN_PATH);

        time_sec = time (NULL);
        gf_time_fmt (timestamp, sizeof timestamp, time_sec, gf_timefmt_FT);
        timelen = strlen (timestamp);

        fd = open (file_path, O_CREAT|O_RDWR, 0644);
        if (fd == -1) {
                op_errno = errno;
                op = "open";
                goto out;
        }
        nofbytes = sys_write (fd, timestamp, timelen);
        if (nofbytes < 0) {
                op_errno = errno;
                op = "write";
                goto out;
        }
        /* Seek the offset to the beginning of the file, so that the offset for
        read is from beginning of file */
        sys_lseek(fd, 0, SEEK_SET);
        nofbytes = sys_read (fd, buff, timelen);
        if (nofbytes == -1) {
                op_errno = errno;
                op = "read";
                goto out;
        }
        ret = 0;
out:
        if (fd != -1) {
                sys_close (fd);
        }
        if (ret && file_path[0]) {
                gf_msg (this->name, GF_LOG_WARNING, errno,
                        P_MSG_HEALTHCHECK_FAILED,
                        "%s() on %s returned", op, file_path);
                gf_event (EVENT_POSIX_HEALTH_CHECK_FAILED,
                          "op=%s;path=%s;error=%s;brick=%s:%s", op, file_path,
                          strerror (op_errno), priv->hostname, priv->base_path);
        }
        return ret;

}

static void *
posix_health_check_thread_proc (void *data)
{
        xlator_t             *this               = NULL;
        struct posix_private *priv               = NULL;
        uint32_t              interval           = 0;
        int                   ret                = -1;

        this = data;
        priv = this->private;

        /* prevent races when the interval is updated */
        interval = priv->health_check_interval;
        if (interval == 0)
                goto out;

        gf_msg_debug (this->name, 0, "health-check thread started, "
                "interval = %d seconds", interval);

        while (1) {
                /* aborting sleep() is a request to exit this thread, sleep()
                 * will normally not return when cancelled */
                ret = sleep (interval);
                if (ret > 0)
                        break;

                /* prevent thread errors while doing the health-check(s) */
                pthread_setcancelstate (PTHREAD_CANCEL_DISABLE, NULL);

                /* Do the health-check.*/
                ret = posix_fs_health_check (this);
                if (ret < 0)
                        goto abort;

                pthread_setcancelstate (PTHREAD_CANCEL_ENABLE, NULL);
        }

out:
        gf_msg_debug (this->name, 0, "health-check thread exiting");

        LOCK (&priv->lock);
        {
                priv->health_check_active = _gf_false;
        }
        UNLOCK (&priv->lock);

        return NULL;

abort:
        /* health-check failed */
        gf_msg (this->name, GF_LOG_EMERG, 0, P_MSG_HEALTHCHECK_FAILED,
                "health-check failed, going down");

        xlator_notify (this->parents->xlator, GF_EVENT_CHILD_DOWN, this);

        ret = sleep (30);
        if (ret == 0) {
                gf_msg (this->name, GF_LOG_EMERG, 0, P_MSG_HEALTHCHECK_FAILED,
                        "still alive! -> SIGTERM");
                kill (getpid(), SIGTERM);
        }

        ret = sleep (30);
        if (ret == 0) {
                gf_msg (this->name, GF_LOG_EMERG, 0, P_MSG_HEALTHCHECK_FAILED,
                        "still alive! -> SIGKILL");
                kill (getpid(), SIGKILL);
        }

        return NULL;
}

void
posix_spawn_health_check_thread (xlator_t *xl)
{
        struct posix_private *priv               = NULL;
        int                   ret                = -1;

        priv = xl->private;

        LOCK (&priv->lock);
        {
                /* cancel the running thread  */
                if (priv->health_check_active == _gf_true) {
                        pthread_cancel (priv->health_check);
                        priv->health_check_active = _gf_false;
                }

                /* prevent scheduling a check in a tight loop */
                if (priv->health_check_interval == 0)
                        goto unlock;

                ret = gf_thread_create (&priv->health_check, NULL,
                                        posix_health_check_thread_proc, xl);
                if (ret < 0) {
                        priv->health_check_interval = 0;
                        priv->health_check_active = _gf_false;
                        gf_msg (xl->name, GF_LOG_ERROR, errno,
                                P_MSG_HEALTHCHECK_FAILED,
                                "unable to setup health-check thread");
                        goto unlock;
                }

                /* run the thread detached, resources will be freed on exit */
                pthread_detach (priv->health_check);
                priv->health_check_active = _gf_true;
        }
unlock:
        UNLOCK (&priv->lock);
}

int
posix_fsyncer_pick (xlator_t *this, struct list_head *head)
{
        struct posix_private *priv = NULL;
        int count = 0;

        priv = this->private;
        pthread_mutex_lock (&priv->fsync_mutex);
        {
                while (list_empty (&priv->fsyncs))
                        pthread_cond_wait (&priv->fsync_cond,
                                           &priv->fsync_mutex);

                count = priv->fsync_queue_count;
                priv->fsync_queue_count = 0;
                list_splice_init (&priv->fsyncs, head);
        }
        pthread_mutex_unlock (&priv->fsync_mutex);

        return count;
}


void
posix_fsyncer_process (xlator_t *this, call_stub_t *stub, gf_boolean_t do_fsync)
{
        struct posix_fd *pfd = NULL;
        int ret = -1;
        int op_errno = 0;

        ret = posix_fd_ctx_get (stub->args.fd, this, &pfd, &op_errno);
        if (ret < 0) {
                gf_msg (this->name, GF_LOG_ERROR, op_errno,
                        P_MSG_GET_FDCTX_FAILED,
                        "could not get fdctx for fd(%s)",
                        uuid_utoa (stub->args.fd->inode->gfid));
                call_unwind_error (stub, -1, op_errno);
                return;
        }

        if (do_fsync) {
                if (stub->args.datasync)
                        ret = sys_fdatasync (pfd->fd);
                else
                        ret = sys_fsync (pfd->fd);
        } else {
                ret = 0;
        }

        if (ret) {
                gf_msg (this->name, GF_LOG_ERROR, errno, P_MSG_FSTAT_FAILED,
                        "could not fstat fd(%s)",
                        uuid_utoa (stub->args.fd->inode->gfid));
                call_unwind_error (stub, -1, errno);
                return;
        }

        call_unwind_error (stub, 0, 0);
}


static void
posix_fsyncer_syncfs (xlator_t *this, struct list_head *head)
{
        call_stub_t *stub = NULL;
        struct posix_fd *pfd = NULL;
        int ret = -1;

        stub = list_entry (head->prev, call_stub_t, list);
        ret = posix_fd_ctx_get (stub->args.fd, this, &pfd, NULL);
        if (ret)
                return;

#ifdef GF_LINUX_HOST_OS
        /* syncfs() is not "declared" in RHEL's glibc even though
           the kernel has support.
        */
#include <sys/syscall.h>
#include <unistd.h>
#ifdef SYS_syncfs
        syscall (SYS_syncfs, pfd->fd);
#else
        sync();
#endif
#else
        sync();
#endif
}


void *
posix_fsyncer (void *d)
{
        xlator_t *this = d;
        struct posix_private *priv = NULL;
        call_stub_t *stub = NULL;
        call_stub_t *tmp = NULL;
        struct list_head list;
        int count = 0;
        gf_boolean_t do_fsync = _gf_true;

        priv = this->private;

        for (;;) {
                INIT_LIST_HEAD (&list);

                count = posix_fsyncer_pick (this, &list);

                usleep (priv->batch_fsync_delay_usec);

                gf_msg_debug (this->name, 0,
                        "picked %d fsyncs", count);

                switch (priv->batch_fsync_mode) {
                case BATCH_NONE:
                case BATCH_REVERSE_FSYNC:
                        break;
                case BATCH_SYNCFS:
                case BATCH_SYNCFS_SINGLE_FSYNC:
                case BATCH_SYNCFS_REVERSE_FSYNC:
                        posix_fsyncer_syncfs (this, &list);
                        break;
                }

                if (priv->batch_fsync_mode == BATCH_SYNCFS)
                        do_fsync = _gf_false;
                else
                        do_fsync = _gf_true;

                list_for_each_entry_safe_reverse (stub, tmp, &list, list) {
                        list_del_init (&stub->list);

                        posix_fsyncer_process (this, stub, do_fsync);

                        if (priv->batch_fsync_mode == BATCH_SYNCFS_SINGLE_FSYNC)
                                do_fsync = _gf_false;
                }
        }
}

/**
 * TODO: move fd/inode interfaces into a single routine..
 */
static int32_t
posix_fetch_signature_xattr (char *real_path,
                             const char *key, dict_t *xattr, size_t *xsize)
{
        int32_t ret = 0;
        char    *memptr    = NULL;
        ssize_t  xattrsize = 0;

        xattrsize = sys_lgetxattr (real_path, key, NULL, 0);
        if ((xattrsize == -1) && ((errno == ENOATTR) || (errno == ENODATA)))
                return 0;
        if (xattrsize == -1)
                goto error_return;

        memptr = GF_CALLOC (xattrsize + 1, sizeof (char), gf_posix_mt_char);
        if (!memptr)
                goto error_return;
        ret = sys_lgetxattr (real_path, key, memptr, xattrsize);
        if (ret == -1)
                goto freemem;

        ret = dict_set_dynptr (xattr, (char *)key, memptr, xattrsize);
        if (ret)
                goto freemem;

        if (xsize)
                *xsize = xattrsize;

        return 0;

 freemem:
        GF_FREE (memptr);
 error_return:
        return -1;
}

static int32_t
posix_fd_fetch_signature_xattr (int fd,
                                const char *key, dict_t *xattr, size_t *xsize)
{
        int32_t ret = 0;
        char    *memptr    = NULL;
        ssize_t  xattrsize = 0;

        xattrsize = sys_fgetxattr (fd, key, NULL, 0);
        if ((xattrsize == -1) && ((errno == ENOATTR) || (errno == ENODATA)))
                return 0;
        if (xattrsize == -1)
                goto error_return;

        memptr = GF_CALLOC (xattrsize + 1, sizeof (char), gf_posix_mt_char);
        if (!memptr)
                goto error_return;
        ret = sys_fgetxattr (fd, key, memptr, xattrsize);
        if (ret == -1)
                goto freemem;

        ret = dict_set_dynptr (xattr, (char *)key, memptr, xattrsize);
        if (ret)
                goto freemem;

        if (xsize)
                *xsize = xattrsize;

        return 0;

 freemem:
        GF_FREE (memptr);
 error_return:
        return -1;
}

/**
 * Fetch on-disk ongoing version and object signature extended attribute.
 * Be generous to absence of xattrs (just *absence*, other errors are
 * propagated up to the invoker), higher layer (br-stub) takes care of
 * interpreting the xattrs for anomalies.
 */
int32_t
posix_get_objectsignature (char *real_path, dict_t *xattr)
{
        int32_t ret = 0;
        size_t signsize = 0;

        ret = posix_fetch_signature_xattr
                (real_path, BITROT_CURRENT_VERSION_KEY, xattr, NULL);
        if (ret)
                goto error_return;

        ret = posix_fetch_signature_xattr
                (real_path, BITROT_SIGNING_VERSION_KEY, xattr, &signsize);
        if (ret)
                goto delkey1;

        ret = dict_set_uint32
                  (xattr, BITROT_SIGNING_XATTR_SIZE_KEY, (uint32_t) signsize);
        if (ret)
                goto delkey2;

        return 0;

 delkey2:
        dict_del (xattr, BITROT_SIGNING_VERSION_KEY);
 delkey1:
        dict_del (xattr, BITROT_CURRENT_VERSION_KEY);
 error_return:
        return -EINVAL;
}

int32_t
posix_fdget_objectsignature (int fd, dict_t *xattr)
{
        int32_t ret = 0;
        size_t signsize = 0;

        ret = posix_fd_fetch_signature_xattr
                (fd, BITROT_CURRENT_VERSION_KEY, xattr, NULL);
        if (ret)
                goto error_return;

        ret = posix_fd_fetch_signature_xattr
                (fd, BITROT_SIGNING_VERSION_KEY, xattr, &signsize);
        if (ret)
                goto delkey1;

        ret = dict_set_uint32
                  (xattr, BITROT_SIGNING_XATTR_SIZE_KEY, (uint32_t) signsize);
        if (ret)
                goto delkey2;

        return 0;

 delkey2:
        dict_del (xattr, BITROT_SIGNING_VERSION_KEY);
 delkey1:
        dict_del (xattr, BITROT_CURRENT_VERSION_KEY);
 error_return:
        return -EINVAL;
}


int
posix_inode_ctx_get (inode_t *inode, xlator_t *this, uint64_t *ctx)
{
        int             ret     = -1;
        uint64_t        ctx_int = 0;

        GF_VALIDATE_OR_GOTO (this->name, this, out);
        GF_VALIDATE_OR_GOTO (this->name, inode, out);

        ret = inode_ctx_get (inode, this, &ctx_int);

        if (ret)
                return ret;

        if (ctx)
                *ctx = ctx_int;

out:
        return ret;
}


int
posix_inode_ctx_set (inode_t *inode, xlator_t *this, uint64_t ctx)
{
        int             ret = -1;

        GF_VALIDATE_OR_GOTO (this->name, this, out);
        GF_VALIDATE_OR_GOTO (this->name, inode, out);
        GF_VALIDATE_OR_GOTO (this->name, ctx, out);

        ret = inode_ctx_set (inode, this, &ctx);
out:
        return ret;
}
