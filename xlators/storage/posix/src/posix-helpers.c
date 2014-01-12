/*
   Copyright (c) 2006-2012 Red Hat, Inc. <http://www.redhat.com>
   This file is part of GlusterFS.

   This file is licensed to you under your choice of the GNU Lesser
   General Public License, version 3 or any later version (LGPLv3 or
   later), or the GNU General Public License, version 2 (GPLv2), in all
   cases as published by the Free Software Foundation.
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
#include <signal.h>

#ifndef GF_BSD_HOST_OS
#include <alloca.h>
#endif /* GF_BSD_HOST_OS */

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
#include <fnmatch.h>

char *marker_xattrs[] = {"trusted.glusterfs.quota.*",
                         "trusted.glusterfs.*.xtime",
                         NULL};

char *marker_contri_key = "trusted.*.*.contri";

static char* posix_ignore_xattrs[] = {
        "gfid-req",
        GLUSTERFS_ENTRYLK_COUNT,
        GLUSTERFS_INODELK_COUNT,
        GLUSTERFS_POSIXLK_COUNT,
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
posix_xattr_ignorable (char *key, posix_xattr_filler_t *filler)
{
        int          i = 0;
        gf_boolean_t ignore = _gf_false;

        GF_ASSERT (key);
        if (!key)
                goto out;
        for (i = 0; posix_ignore_xattrs[i]; i++) {
                if (!strcmp (key, posix_ignore_xattrs[i])) {
                        ignore = _gf_true;
                        goto out;
                }
        }
        if ((!strcmp (key, GF_CONTENT_KEY))
            && (!IA_ISREG (filler->stbuf->ia_type)))
                ignore = _gf_true;
out:
        return ignore;
}

static int
_posix_xattr_get_set_from_backend (posix_xattr_filler_t *filler, char *key)
{
        ssize_t  xattr_size = -1;
        int      ret        = 0;
        char    *value      = NULL;

        xattr_size = sys_lgetxattr (filler->real_path, key, NULL, 0);

        if (xattr_size > 0) {
                value = GF_CALLOC (1, xattr_size + 1,
                                   gf_posix_mt_char);
                if (!value)
                        goto out;

                xattr_size = sys_lgetxattr (filler->real_path, key, value,
                                            xattr_size);
                if (xattr_size <= 0) {
                        gf_log (filler->this->name, GF_LOG_WARNING,
                                "getxattr failed. path: %s, key: %s",
                                filler->real_path, key);
                        GF_FREE (value);
                        goto out;
                }

                value[xattr_size] = '\0';
                ret = dict_set_bin (filler->xattr, key,
                                    value, xattr_size);
                if (ret < 0) {
                        gf_log (filler->this->name, GF_LOG_DEBUG,
                                "dict set failed. path: %s, key: %s",
                                filler->real_path, key);
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

        size = sys_llistxattr (filler->real_path, NULL, 0);
        if (size == -1) {
                if ((errno == ENOTSUP) || (errno == ENOSYS)) {
                        GF_LOG_OCCASIONALLY (gf_posix_xattr_enotsup_log,
                                             THIS->name, GF_LOG_WARNING,
                                             "Extended attributes not "
                                             "supported (try remounting brick"
                                             " with 'user_xattr' flag)");

                } else {
                        gf_log (THIS->name, GF_LOG_WARNING,
                                "listxattr failed on %s: %s",
                                filler->real_path, strerror (errno));

                }

                goto out;
        }

        if (size == 0) {
                ret = 0;
                goto out;
        }

        list = alloca (size + 1);
        if (!list) {
                goto out;
        }

        size = sys_llistxattr (filler->real_path, list, size);
        if (size <= 0) {
                ret = size;
                goto out;
        }

        remaining_size = size;
        list_offset = 0;

        while (remaining_size > 0) {
                if (*(list + list_offset) == '\0')
                        break;
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

static int
_posix_xattr_get_set (dict_t *xattr_req,
                      char *key,
                      data_t *data,
                      void *xattrargs)
{
        posix_xattr_filler_t *filler = xattrargs;
        int       ret      = -1;
        char     *databuf  = NULL;
        int       _fd      = -1;
        loc_t    *loc      = NULL;
        ssize_t  req_size  = 0;


        if (posix_xattr_ignorable (key, filler))
                goto out;
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
                                gf_log (filler->this->name, GF_LOG_ERROR,
                                        "failed to set dict value. key: %s, path: %s",
                                        key, filler->real_path);
                                goto err;
                        }

                        /* To avoid double free in cleanup below */
                        databuf = NULL;
                err:
                        if (_fd != -1)
                                close (_fd);
                        GF_FREE (databuf);
                }
        } else if (!strcmp (key, GLUSTERFS_OPEN_FD_COUNT)) {
                loc = filler->loc;
                if (loc) {
                        ret = dict_set_uint32 (filler->xattr, key,
                                               loc->inode->fd_count);
                        if (ret < 0)
                                gf_log (filler->this->name, GF_LOG_WARNING,
                                        "Failed to set dictionary value for %s",
                                        key);
                }
        } else if (!strcmp (key, GET_ANCESTRY_PATH_KEY)) {
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
        uint64_t temp_ino = 0;
        int j = 0;
        int i = 0;

        /* consider least significant 8 bytes of value out of gfid */
        if (uuid_is_null (buf->ia_gfid)) {
                buf->ia_ino = -1;
                goto out;
        }
        for (i = 15; i > (15 - 8); i--) {
		temp_ino += (uint64_t)(buf->ia_gfid[i]) << j;
                j += 8;
        }
        buf->ia_ino = temp_ino;
out:
        return;
}

int
posix_fdstat (xlator_t *this, int fd, struct iatt *stbuf_p)
{
        int                    ret     = 0;
        struct stat            fstatbuf = {0, };
        struct iatt            stbuf = {0, };

        ret = fstat (fd, &fstatbuf);
        if (ret == -1)
                goto out;

        if (fstatbuf.st_nlink && !S_ISDIR (fstatbuf.st_mode))
                fstatbuf.st_nlink--;

        iatt_from_stat (&stbuf, &fstatbuf);

        ret = posix_fill_gfid_fd (this, fd, &stbuf);
        if (ret)
                gf_log_callingfn (this->name, GF_LOG_DEBUG, "failed to get gfid");

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

        ret = lstat (real_path, &lstatbuf);

        if (ret != 0) {
                if (ret == -1) {
                        if (errno != ENOENT && errno != ELOOP)
                                gf_log (this->name, GF_LOG_WARNING,
                                        "lstat failed on %s (%s)",
                                        real_path, strerror (errno));
                } else {
                        // may be some backend filesystem issue
                        gf_log (this->name, GF_LOG_ERROR, "lstat failed on "
                                "%s and return value is %d instead of -1. "
                                "Please see dmesg output to check whether the "
                                "failure is due to backend filesystem issue",
                                real_path, ret);
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
                uuid_copy (stbuf.ia_gfid, gfid);

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

        ret = lstat (path, &lstatbuf);

        if (ret != 0) {
                if (ret == -1) {
                        if (errno != ENOENT)
                                gf_log (this->name, GF_LOG_WARNING,
                                        "lstat failed on %s (%s)",
                                        path, strerror (errno));
                } else {
                        // may be some backend filesytem issue
                        gf_log (this->name, GF_LOG_ERROR, "lstat failed on "
                                "%s and return value is %d instead of -1. "
                                "Please see dmesg output to check whether the "
                                "failure is due to backend filesystem issue",
                                path, ret);
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

        if (gfid && !uuid_is_null (gfid))
                uuid_copy (stbuf.ia_gfid, gfid);
        else
                posix_fill_gfid_path (this, path, &stbuf);

        posix_fill_ino_from_gfid (this, &stbuf);

        if (buf_p)
                *buf_p = stbuf;
out:
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
                gf_log (this->name, GF_LOG_DEBUG,
                        "failed to get the gfid from dict for %s",
                        loc->path);
                goto out;
        }

        ret = sys_lsetxattr (path, GFID_XATTR_KEY, uuid_req, 16, XATTR_CREATE);
        if (ret != 0) {
                gf_log (this->name, GF_LOG_WARNING,
                        "setting GFID on %s failed (%s)", path,
                        strerror (errno));
                goto out;
        }
        uuid_copy (uuid_curr, uuid_req);

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
                        ret = write (file_fd, value->data, value->len);
                        if (ret == -1) {
                                op_ret = -errno;
                                gf_log (this->name, GF_LOG_ERROR,
                                        "write failed while doing setxattr "
                                        "for key %s on path %s: %s",
                                        key, real_path, strerror (errno));
                                goto out;
                        }

                        ret = close (file_fd);
                        if (ret == -1) {
                                op_ret = -errno;
                                gf_log (this->name, GF_LOG_ERROR,
                                        "close failed on %s: %s",
                                        real_path, strerror (errno));
                                goto out;
                        }
                }

        create: /* we know file doesn't exist, create it */

                file_fd = open (real_path, O_CREAT|O_WRONLY, 0644);

                if (file_fd == -1) {
                        op_ret = -errno;
                        gf_log (this->name, GF_LOG_ERROR,
                                "failed to open file %s with O_CREAT: %s",
                                key, strerror (errno));
                        goto out;
                }

                ret = write (file_fd, value->data, value->len);
                if (ret == -1) {
                        op_ret = -errno;
                        gf_log (this->name, GF_LOG_ERROR,
                                "write failed on %s while setxattr with "
                                "key %s: %s",
                                real_path, key, strerror (errno));
                        goto out;
                }

                ret = close (file_fd);
                if (ret == -1) {
                        op_ret = -errno;
                        gf_log (this->name, GF_LOG_ERROR,
                                "close failed on %s while setxattr with "
                                "key %s: %s",
                                real_path, key, strerror (errno));
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

        op_ret = posix_istat (this, pargfid, name, &stbuf);
        if (op_ret == -1) {
                op_ret = -errno;
                gf_log (this->name, GF_LOG_ERROR, "lstat failed on %s: %s",
                        real_path, strerror (errno));
                goto out;
        }

        file_fd = open (real_path, O_RDONLY);

        if (file_fd == -1) {
                op_ret = -errno;
                gf_log (this->name, GF_LOG_ERROR, "open failed on %s: %s",
                        real_path, strerror (errno));
                goto out;
        }

        *contents = GF_CALLOC (stbuf.ia_size + 1, sizeof(char),
                               gf_posix_mt_char);
        if (! *contents) {
                op_ret = -errno;
                goto out;
        }

        ret = read (file_fd, *contents, stbuf.ia_size);
        if (ret <= 0) {
                op_ret = -1;
                gf_log (this->name, GF_LOG_ERROR, "read on %s failed: %s",
                        real_path, strerror (errno));
                goto out;
        }

        *contents[stbuf.ia_size] = '\0';

        op_ret = close (file_fd);
        file_fd = -1;
        if (op_ret == -1) {
                op_ret = -errno;
                gf_log (this->name, GF_LOG_ERROR, "close on %s failed: %s",
                        real_path, strerror (errno));
                goto out;
        }

out:
        if (op_ret < 0) {
                GF_FREE (*contents);
                if (file_fd != -1)
                        close (file_fd);
        }

        return op_ret;
}

static int gf_xattr_enotsup_log;

int
posix_handle_pair (xlator_t *this, const char *real_path,
                   char *key, data_t *value, int flags)
{
        int sys_ret = -1;
        int ret     = 0;

        if (XATTR_IS_PATHINFO (key)) {
                ret = -EACCES;
                goto out;
        } else if (ZR_FILE_CONTENT_REQUEST(key)) {
                ret = posix_set_file_contents (this, real_path, key, value,
                                               flags);
        } else {
                sys_ret = sys_lsetxattr (real_path, key, value->data,
                                         value->len, flags);

                if (sys_ret < 0) {
                        ret = -errno;
                        if (errno == ENOTSUP) {
                                GF_LOG_OCCASIONALLY(gf_xattr_enotsup_log,
                                                    this->name,GF_LOG_WARNING,
                                                    "Extended attributes not "
                                                    "supported (try remounting "
                                                    "brick with 'user_xattr' "
                                                    "flag)");
                        } else if (errno == ENOENT) {
                                if (!posix_special_xattr (marker_xattrs,
                                                          key)) {
                                        gf_log (this->name, GF_LOG_ERROR,
                                                "setxattr on %s failed: %s",
                                                real_path, strerror (errno));
                                }
                        } else {

#ifdef GF_DARWIN_HOST_OS
                                gf_log (this->name,
                                        ((errno == EINVAL) ?
                                         GF_LOG_DEBUG : GF_LOG_ERROR),
                                        "%s: key:%s error:%s",
                                        real_path, key,
                                        strerror (errno));
#else /* ! DARWIN */
                                gf_log (this->name, GF_LOG_ERROR,
                                        "%s: key:%s error:%s",
                                        real_path, key,
                                        strerror (errno));
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
                    char *key, data_t *value, int flags)
{
        int sys_ret = -1;
        int ret     = 0;

        if (XATTR_IS_PATHINFO (key)) {
                ret = -EACCES;
                goto out;
        }

        sys_ret = sys_fsetxattr (fd, key, value->data,
                                 value->len, flags);

        if (sys_ret < 0) {
                ret = -errno;
                if (errno == ENOTSUP) {
                        GF_LOG_OCCASIONALLY(gf_xattr_enotsup_log,
                                            this->name,GF_LOG_WARNING,
                                            "Extended attributes not "
                                            "supported (try remounting "
                                            "brick with 'user_xattr' "
                                            "flag)");
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
                                fd, key, strerror (errno));
#else /* ! DARWIN */
                        gf_log (this->name, GF_LOG_ERROR,
                                "fd=%d: key:%s error:%s",
                                fd, key, strerror (errno));
#endif /* DARWIN */
                }

                goto out;
        }

out:
        return ret;
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
                gf_log (THIS->name, GF_LOG_TRACE,
                        "unlinking %s", fpath);
                unlink (fpath);
                if (stbuf.ia_nlink == 1)
                        posix_handle_unset (this, stbuf.ia_gfid, NULL);
                break;

        case S_IFDIR:
                if (ftwbuf->level) { /* don't remove top level dir */
                        gf_log (THIS->name, GF_LOG_TRACE,
                                "removing directory %s", fpath);

                        rmdir (fpath);
                        posix_handle_unset (this, stbuf.ia_gfid, NULL);
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
                                "janitor cleaning out %s", priv->trash_path);

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
                if (ret != 0)
                        goto out;
        }

        data = dict_get (xattr_req, POSIX_ACL_DEFAULT_XATTR);
        if (data) {
                ret = sys_lsetxattr (path, POSIX_ACL_DEFAULT_XATTR,
                                     data->data, data->len, 0);
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
            ZR_FILE_CONTENT_REQUEST(k)) {
                return 0;
        }

        ret = posix_handle_pair (filler->this, filler->real_path, k, v,
                                 XATTR_CREATE);
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

        ret = dict_foreach (dict, _handle_entry_create_keyvalue_pair, &filler);

out:
        return ret;
}


static int
__posix_fd_ctx_get (fd_t *fd, xlator_t *this, struct posix_fd **pfd_p)
{
        uint64_t          tmp_pfd = 0;
        struct posix_fd  *pfd = NULL;
        int               ret = -1;
        char             *real_path = NULL;
        int               _fd = -1;
        DIR              *dir = NULL;

        ret = __fd_ctx_get (fd, this, &tmp_pfd);
        if (ret == 0) {
                pfd = (void *)(long) tmp_pfd;
                ret = 0;
                goto out;
        }

        if (!fd_is_anonymous(fd))
                /* anonymous fd */
                goto out;

        MAKE_HANDLE_PATH (real_path, this, fd->inode->gfid, NULL);

        pfd = GF_CALLOC (1, sizeof (*pfd), gf_posix_mt_posix_fd);
        if (!pfd) {
                goto out;
        }
        pfd->fd = -1;

        if (fd->inode->ia_type == IA_IFDIR) {
                dir = opendir (real_path);
                if (!dir) {
                        GF_FREE (pfd);
                        pfd = NULL;
                        goto out;
                }
                _fd = dirfd (dir);
        }

        if (fd->inode->ia_type == IA_IFREG) {
                _fd = open (real_path, O_RDWR|O_LARGEFILE);
                if (_fd == -1) {
                        GF_FREE (pfd);
                        pfd = NULL;
                        goto out;
                }
        }

        pfd->fd = _fd;
        pfd->dir = dir;

        ret = __fd_ctx_set (fd, this, (uint64_t) (long) pfd);
        if (ret != 0) {
                if (_fd != -1)
                        close (_fd);
                if (dir)
                        closedir (dir);
                GF_FREE (pfd);
                pfd = NULL;
                goto out;
        }

        ret = 0;
out:
        if (pfd_p)
                *pfd_p = pfd;
        return ret;
}


int
posix_fd_ctx_get (fd_t *fd, xlator_t *this, struct posix_fd **pfd)
{
        int   ret;

        LOCK (&fd->inode->lock);
        {
                ret = __posix_fd_ctx_get (fd, this, pfd);
        }
        UNLOCK (&fd->inode->lock);

        return ret;
}

static void *
posix_health_check_thread_proc (void *data)
{
        xlator_t             *this               = NULL;
        struct posix_private *priv               = NULL;
        uint32_t              interval           = 0;
        int                   ret                = -1;
        struct stat           sb                 = {0, };

        this = data;
        priv = this->private;

        /* prevent races when the interval is updated */
        interval = priv->health_check_interval;
        if (interval == 0)
                goto out;

        gf_log (this->name, GF_LOG_DEBUG, "health-check thread started, "
                "interval = %d seconds", interval);

        while (1) {
                /* aborting sleep() is a request to exit this thread, sleep()
                 * will normally not return when cancelled */
                ret = sleep (interval);
                if (ret > 0)
                        break;

                /* prevent thread errors while doing the health-check(s) */
                pthread_setcancelstate (PTHREAD_CANCEL_DISABLE, NULL);

                /* Do the health-check, it should be moved to its own function
                 * in case it gets more complex. */
                ret = stat (priv->base_path, &sb);
                if (ret < 0) {
                        gf_log (this->name, GF_LOG_WARNING,
                                "stat() on %s returned: %s", priv->base_path,
                                strerror (errno));
                        goto abort;
                }

                pthread_setcancelstate (PTHREAD_CANCEL_ENABLE, NULL);
        }

out:
        gf_log (this->name, GF_LOG_DEBUG, "health-check thread exiting");

        LOCK (&priv->lock);
        {
                priv->health_check_active = _gf_false;
        }
        UNLOCK (&priv->lock);

        return NULL;

abort:
        /* health-check failed */
        gf_log (this->name, GF_LOG_EMERG, "health-check failed, going down");
        xlator_notify (this->parents->xlator, GF_EVENT_CHILD_DOWN, this);

        ret = sleep (30);
        if (ret == 0) {
                gf_log (this->name, GF_LOG_EMERG, "still alive! -> SIGTERM");
                kill (getpid(), SIGTERM);
        }

        ret = sleep (30);
        if (ret == 0) {
                gf_log (this->name, GF_LOG_EMERG, "still alive! -> SIGKILL");
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
                        gf_log (xl->name, GF_LOG_ERROR,
                                "unable to setup health-check thread: %s",
                                strerror (errno));
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
	struct posix_private *priv = NULL;

	priv = this->private;

	ret = posix_fd_ctx_get (stub->args.fd, this, &pfd);
	if (ret < 0) {
		gf_log (this->name, GF_LOG_ERROR,
			"could not get fdctx for fd(%s)",
			uuid_utoa (stub->args.fd->inode->gfid));
		call_unwind_error (stub, -1, EINVAL);
		return;
	}

	if (do_fsync) {
#ifdef HAVE_FDATASYNC
		if (stub->args.datasync)
			ret = fdatasync (pfd->fd);
		else
#endif
			ret = fsync (pfd->fd);
	} else {
		ret = 0;
	}

	if (ret) {
		gf_log (this->name, GF_LOG_ERROR,
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
	ret = posix_fd_ctx_get (stub->args.fd, this, &pfd);
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

		gf_log (this->name, GF_LOG_DEBUG,
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
