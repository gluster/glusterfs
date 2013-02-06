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
#include <fnmatch.h>

char *marker_xattrs[] = {"trusted.glusterfs.quota.*",
                         "trusted.glusterfs.*.xtime",
                         NULL};

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
        } else {
                xattr_size = sys_lgetxattr (filler->real_path, key, NULL, 0);

                if (xattr_size > 0) {
                        value = GF_CALLOC (1, xattr_size + 1,
                                           gf_posix_mt_char);
                        if (!value)
                                return -1;

                        xattr_size = sys_lgetxattr (filler->real_path, key, value,
                                                    xattr_size);
                        if (xattr_size <= 0) {
                                gf_log (filler->this->name, GF_LOG_WARNING,
                                        "getxattr failed. path: %s, key: %s",
                                        filler->real_path, key);
                                GF_FREE (value);
                                return -1;
                        }

                        value[xattr_size] = '\0';
                        ret = dict_set_bin (filler->xattr, key,
                                            value, xattr_size);
                        if (ret < 0) {
                                gf_log (filler->this->name, GF_LOG_DEBUG,
                                        "dict set failed. path: %s, key: %s",
                                        filler->real_path, key);
                                GF_FREE (value);
                        }
                }
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

        if (ZR_FILE_CONTENT_REQUEST(key)) {
                ret = posix_set_file_contents (this, real_path, key, value,
                                               flags);
        } else {
                sys_ret = sys_lsetxattr (real_path, key, value->data,
                                         value->len, flags);

                if (sys_ret < 0) {
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

                        ret = -errno;
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

        sys_ret = sys_fsetxattr (fd, key, value->data,
                                 value->len, flags);

        if (sys_ret < 0) {
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

                ret = -errno;
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
posix_acl_xattr_set (xlator_t *this, const char *path, dict_t *xattr_req)
{
        int          ret = 0;
        data_t      *data = NULL;
        struct stat  stat = {0, };

        if (!xattr_req)
                goto out;

        if (sys_lstat (path, &stat) != 0)
                goto out;

        data = dict_get (xattr_req, "system.posix_acl_access");
        if (data) {
                ret = sys_lsetxattr (path, "system.posix_acl_access",
                                     data->data, data->len, 0);
                if (ret != 0)
                        goto out;
        }

        data = dict_get (xattr_req, "system.posix_acl_default");
        if (data) {
                ret = sys_lsetxattr (path, "system.posix_acl_default",
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
            !strcmp ("system.posix_acl_default", k) ||
            !strcmp ("system.posix_acl_access", k) ||
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
