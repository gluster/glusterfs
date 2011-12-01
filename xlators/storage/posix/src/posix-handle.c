/*
  Copyright (c) 2011 Gluster, Inc. <http://www.gluster.com>
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

#include <errno.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <unistd.h>
#include <libgen.h>

#include "posix-handle.h"
#include "posix.h"
#include "xlator.h"


#define HANDLE_PFX ".glusterfs"


int
posix_handle_relpath (xlator_t *this, uuid_t gfid, const char *basename,
                      char *buf, size_t buflen)
{
        char *uuid_str = NULL;
        int   len      = 0;

        len = 3        /* "../" */
                + 3    /* "../" */
                + 3    /* "00/" */
                + 3    /* "00/" */
                + 36   /* "00000000-0000-0000-0000-000000000000" */
                + 1    /* '\0' */
                ;

        if (basename) {
                len += (strlen (basename) + 1);
        }

        if (buflen < len || !buf)
                return len;

        uuid_str = uuid_utoa (gfid);

        if (basename) {
                len = snprintf (buf, buflen, "../../%02x/%02x/%s/%s",
                                gfid[0], gfid[1], uuid_str, basename);
        } else {
                len = snprintf (buf, buflen, "../../%02x/%02x/%s",
                                gfid[0], gfid[1], uuid_str);
        }

        return len;
}


int
posix_handle_path (xlator_t *this, uuid_t gfid, const char *basename, char *buf,
                   size_t buflen)
{
        struct posix_private *priv = NULL;
        char                 *uuid_str = NULL;
        int                   len = 0;
        char                  linkname[300] = {0,}; /* "../../<gfid>/<NAME_MAX>" */
        int                   ret = 0;
        struct stat           stat;

        priv = this->private;

        len = priv->base_path_length  /* option directory "/export" */
                + 1                   /* "/" */
                + 11                  /* ".glusterfs/" */
                + 3                   /* "00/" */
                + 3                   /* "00/" */
                + 36                  /* "00000000-0000-0000-0000-000000000000" */
                + 1                   /* '\0' */
                ;

        if (basename) {
                len += (strlen (basename) + 1);
        } else {
                len += 256;  /* worst-case for directory's symlink-handle expansion */
        }

        if (buflen < len || !buf)
                return len;

        uuid_str = uuid_utoa (gfid);

        if (!__is_root_gfid (gfid)) {
                if (basename) {
                        len = snprintf (buf, buflen, "%s/%s", priv->base_path,
                                        basename);
                } else {
                        strncpy (buf, priv->base_path, buflen);
                }
        }

        if (basename) {
                len = snprintf (buf, buflen, "%s/%s/%02x/%02x/%s/%s", priv->base_path,
                                HANDLE_PFX, gfid[0], gfid[1], uuid_str, basename);
        } else {
                len = snprintf (buf, buflen, "%s/%s/%02x/%02x/%s", priv->base_path,
                                HANDLE_PFX, gfid[0], gfid[1], uuid_str);

                ret = lstat (buf, &stat);

                if (ret == 0 && S_ISLNK(stat.st_mode) && stat.st_nlink == 1) {
                        /* is a directory's symlink-handle */
                        ret = readlink (buf, linkname, buflen);
                        if (ret < 0 || ret > buflen)
                                goto out;
                        linkname[ret] = 0;

                        if (memcmp (linkname, "../../", 6) != 0)
                                goto out;

                        strncpy (buf + priv->base_path_length + 1 + strlen (HANDLE_PFX) + 1,
                                 linkname + 6,
                                 buflen - (priv->base_path_length + 1 + strlen (HANDLE_PFX) + 1));
                }
        }
out:
        return len;
}

int
posix_handle_gfid_path (xlator_t *this, uuid_t gfid, const char *basename,
                        char *buf, size_t buflen)
{
        struct posix_private *priv = NULL;
        char                 *uuid_str = NULL;
        int                   len = 0;

        priv = this->private;

        len = priv->base_path_length  /* option directory "/export" */
                + 1                   /* "/" */
                + 11                  /* ".glusterfs/" */
                + 3                   /* "00/" */
                + 3                   /* "00/" */
                + 36                  /* "00000000-0000-0000-0000-000000000000" */
                + 1                   /* '\0' */
                ;

        if (basename) {
                len += (strlen (basename) + 1);
        } else {
                len += 256;  /* worst-case for directory's symlink-handle expansion */
        }

        if ((buflen < len) || !buf)
                return len;

        uuid_str = uuid_utoa (gfid);

        if (!__is_root_gfid (gfid)) {
                if (basename) {
                        len = snprintf (buf, buflen, "%s/%s", priv->base_path,
                                        basename);
                } else {
                        strncpy (buf, priv->base_path, buflen);
                }
        }

        if (basename) {
                len = snprintf (buf, buflen, "%s/%s/%02x/%02x/%s/%s", priv->base_path,
                                HANDLE_PFX, gfid[0], gfid[1], uuid_str, basename);
        } else {
                len = snprintf (buf, buflen, "%s/%s/%02x/%02x/%s", priv->base_path,
                                HANDLE_PFX, gfid[0], gfid[1], uuid_str);
        }

        return len;
}


int
posix_handle_init (xlator_t *this)
{
        struct posix_private *priv = NULL;
        char                 *handle_pfx = NULL;
        int                   ret = 0;
        int                   len = 0;
        struct stat           stbuf;
        struct stat           rootbuf;
        struct stat           exportbuf;
        char                 *rootstr = NULL;
        uuid_t                gfid = {0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,1};

        priv = this->private;

        ret = stat (priv->base_path, &exportbuf);
        if (ret || !S_ISDIR (exportbuf.st_mode)) {
                gf_log (this->name, GF_LOG_ERROR,
                        "Not a directory: %s", priv->base_path);
                return -1;
        }

        handle_pfx = alloca (priv->base_path_length + 1 + strlen (HANDLE_PFX)
                             + 1);

        sprintf (handle_pfx, "%s/%s", priv->base_path, HANDLE_PFX);

        ret = stat (handle_pfx, &stbuf);
        switch (ret) {
        case -1:
                if (errno == ENOENT) {
                        ret = mkdir (handle_pfx, 0600);
                        if (ret != 0) {
                                gf_log (this->name, GF_LOG_ERROR,
                                        "Creating directory %s failed: %s",
                                        handle_pfx, strerror (errno));
                                return -1;
                        }
                } else {
                        gf_log (this->name, GF_LOG_ERROR,
                                "Checking for %s failed: %s",
                                handle_pfx, strerror (errno));
                        return -1;
                }
                break;
        case 0:
                if (!S_ISDIR (stbuf.st_mode)) {
                        gf_log (this->name, GF_LOG_ERROR,
                                "Not a directory: %s",
                                handle_pfx);
                        return -1;
                }
                break;
        default:
                break;
        }

        stat (handle_pfx, &priv->handledir);

        len = posix_handle_path (this, gfid, NULL, NULL, 0);
        rootstr = alloca (len);
        posix_handle_path (this, gfid, NULL, rootstr, len);

        ret = stat (rootstr, &rootbuf);
        switch (ret) {
        case -1:
                if (errno != ENOENT) {
                        gf_log (this->name, GF_LOG_ERROR,
                                "%s: %s", priv->base_path,
                                strerror (errno));
                        return -1;
                }

                ret = posix_handle_mkdir_hashes (this, rootstr);
                if (ret) {
                        gf_log (this->name, GF_LOG_WARNING,
                                "mkdir %s failed (%s)",
                                rootstr, strerror (errno));
                        return -1;
                }

                ret = symlink ("../../..", rootstr);
                if (ret) {
                        gf_log (this->name, GF_LOG_ERROR,
                                "symlink %s creation failed (%s)",
                                rootstr, strerror (errno));
                        return -1;
                }
                break;
        case 0:
                if ((exportbuf.st_ino == rootbuf.st_ino) &&
                    (exportbuf.st_dev == rootbuf.st_dev))
                        return 0;

                gf_log (this->name, GF_LOG_ERROR,
                        "Different dirs %s (%lld/%lld) != %s (%lld/%lld)",
                        priv->base_path, (long long) exportbuf.st_ino,
                        (long long) exportbuf.st_dev, rootstr,
                        (long long) rootbuf.st_ino, (long long) rootbuf.st_dev);
                return -1;

                break;
        }

        return 0;
}


int
posix_handle_mkdir_hashes (xlator_t *this, const char *newpath)
{
        char        *duppath = NULL;
        char        *parpath = NULL;
        int          ret = 0;

        duppath = strdupa (newpath);
        parpath = dirname (duppath);
        parpath = dirname (duppath);

        ret = mkdir (parpath, 0700);
        if (ret == -1 && errno != EEXIST) {
                gf_log (this->name, GF_LOG_ERROR,
                        "error mkdir hash-1 %s (%s)",
                        parpath, strerror (errno));
                return -1;
        }

        strcpy (duppath, newpath);
        parpath = dirname (duppath);

        ret = mkdir (parpath, 0700);
        if (ret == -1 && errno != EEXIST) {
                gf_log (this->name, GF_LOG_ERROR,
                        "error mkdir hash-2 %s (%s)",
                        parpath, strerror (errno));
                return -1;
        }

        return 0;
}


int
posix_handle_hard (xlator_t *this, const char *oldpath, uuid_t gfid, struct stat *oldbuf)
{
        char        *newpath = NULL;
        struct stat  newbuf;
        int          ret = -1;


        MAKE_HANDLE_PATH (newpath, this, gfid, NULL);

        ret = lstat (newpath, &newbuf);
        if (ret == -1 && errno != ENOENT) {
                gf_log (this->name, GF_LOG_WARNING,
                        "%s: %s", newpath, strerror (errno));
                return -1;
        }

        if (ret == -1 && errno == ENOENT) {
                ret = posix_handle_mkdir_hashes (this, newpath);
                if (ret) {
                        gf_log (this->name, GF_LOG_WARNING,
                                "mkdir %s failed (%s)",
                                newpath, strerror (errno));
                        return -1;
                }

                ret = link (oldpath, newpath);
                if (ret) {
                        gf_log (this->name, GF_LOG_WARNING,
                                "link %s -> %s failed (%s)",
                                oldpath, newpath, strerror (errno));
                        return -1;
                }

                ret = lstat (newpath, &newbuf);
                if (ret) {
                        gf_log (this->name, GF_LOG_WARNING,
                                "lstat on %s failed (%s)",
                                newpath, strerror (errno));
                        return -1;
                }
        }

        ret = lstat (newpath, &newbuf);
        if (ret) {
                gf_log (this->name, GF_LOG_WARNING,
                        "lstat on %s failed (%s)", newpath, strerror (errno));
                return -1;
        }

        if (newbuf.st_ino != oldbuf->st_ino ||
            newbuf.st_dev != oldbuf->st_dev) {
                gf_log (this->name, GF_LOG_WARNING,
                        "mismatching ino/dev between file %s (%lld/%lld) "
                        "and handle %s (%lld/%lld)",
                        oldpath, (long long) oldbuf->st_ino, (long long) oldbuf->st_dev,
                        newpath, (long long) newbuf.st_ino, (long long) newbuf.st_dev);
                ret = -1;
        }

        return ret;
}


int
posix_handle_soft (xlator_t *this, const char *real_path, loc_t *loc,
                   uuid_t gfid, struct stat *oldbuf)
{
        char        *oldpath = NULL;
        char        *newpath = NULL;
        struct stat  newbuf;
        int          ret = -1;


        MAKE_HANDLE_PATH (newpath, this, gfid, NULL);
        MAKE_HANDLE_RELPATH (oldpath, this, loc->pargfid, loc->name);


        ret = lstat (newpath, &newbuf);
        if (ret == -1 && errno != ENOENT) {
                gf_log (this->name, GF_LOG_WARNING,
                        "%s: %s", newpath, strerror (errno));
                return -1;
        }

        if (ret == -1 && errno == ENOENT) {
                ret = posix_handle_mkdir_hashes (this, newpath);
                if (ret) {
                        gf_log (this->name, GF_LOG_WARNING,
                                "mkdir %s failed (%s)",
                                newpath, strerror (errno));
                        return -1;
                }

                ret = symlink (oldpath, newpath);
                if (ret) {
                        gf_log (this->name, GF_LOG_WARNING,
                                "symlink %s -> %s failed (%s)",
                                oldpath, newpath, strerror (errno));
                        return -1;
                }

                ret = lstat (newpath, &newbuf);
                if (ret) {
                        gf_log (this->name, GF_LOG_WARNING,
                                "stat on %s failed (%s)",
                                newpath, strerror (errno));
                        return -1;
                }
        }

        ret = stat (real_path, &newbuf);
        if (ret) {
                gf_log (this->name, GF_LOG_WARNING,
                        "stat on %s failed (%s)", newpath, strerror (errno));
                return -1;
        }

        if (!oldbuf)
                return ret;

        if (newbuf.st_ino != oldbuf->st_ino ||
            newbuf.st_dev != oldbuf->st_dev) {
                gf_log (this->name, GF_LOG_WARNING,
                        "mismatching ino/dev between file %s (%lld/%lld) "
                        "and handle %s (%lld/%lld)",
                        oldpath, (long long) oldbuf->st_ino, (long long) oldbuf->st_dev,
                        newpath, (long long) newbuf.st_ino, (long long) newbuf.st_dev);
                ret = -1;
        }

        return ret;
}


static int
posix_handle_unset_gfid (xlator_t *this, uuid_t gfid)
{
        char        *path = NULL;
        int          ret = 0;
        struct stat  stat;

        MAKE_HANDLE_GFID_PATH (path, this, gfid, NULL);

        ret = lstat (path, &stat);

        if (ret == -1) {
                if (errno != ENOENT) {
                        gf_log (this->name, GF_LOG_WARNING,
                                "%s: %s", path, strerror (errno));
                }
                goto out;
        }

        ret = unlink (path);
        if (ret == -1) {
                gf_log (this->name, GF_LOG_WARNING,
                        "unlink %s failed (%s)", path, strerror (errno));
        }

out:
        return ret;
}


int
posix_handle_unset (xlator_t *this, uuid_t gfid, const char *basename)
{
        int          ret;
        struct iatt  stat;
        char        *path = NULL;

        MAKE_HANDLE_PATH (path, this, gfid, basename);

        if (!basename) {
                ret = posix_handle_unset_gfid (this, gfid);
                return ret;
        }

        ret = posix_istat (this, gfid, basename, &stat);

        if (ret == -1) {
                gf_log (this->name, GF_LOG_WARNING,
                        "%s: %s", path, strerror (errno));
                return -1;
        }

        ret = posix_handle_unset_gfid (this, stat.ia_gfid);

        return ret;
}


int
posix_handle_expand (xlator_t *this, char *buf, uuid_t gfid,
                     const char *basename)
{
        return 0;
}

