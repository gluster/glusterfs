/*
   Copyright (c) 2011-2012 Red Hat, Inc. <http://www.redhat.com>
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

#include <errno.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <unistd.h>
#include <libgen.h>
#ifdef GF_LINUX_HOST_OS
#include <alloca.h>
#endif

#include "posix-handle.h"
#include "posix.h"
#include "xlator.h"
#include "syscall.h"


#define HANDLE_PFX ".glusterfs"
#define TRASH_DIR "landfill"

#define UUID0_STR "00000000-0000-0000-0000-000000000000"
#define SLEN(str) (sizeof(str) - 1)


int
posix_handle_relpath (xlator_t *this, uuid_t gfid, const char *basename,
                      char *buf, size_t buflen)
{
        char *uuid_str = NULL;
        int   len      = 0;

        len = SLEN("../")
                + SLEN("../")
                + SLEN("00/")
                + SLEN("00/")
                + SLEN(UUID0_STR)
                + 1 /* '\0' */
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


/*
  TODO: explain how this pump fixes ELOOP
*/
int
posix_handle_pump (xlator_t *this, char *buf, int len, int maxlen,
                   char *base_str, int base_len, int pfx_len)
{
        char       linkname[512] = {0,}; /* "../../<gfid>/<NAME_MAX>" */
        int        ret = 0;
        int        blen = 0;
        int        link_len = 0;

        /* is a directory's symlink-handle */
        ret = readlink (base_str, linkname, 512);
        if (ret == -1) {
                gf_log (this->name, GF_LOG_ERROR,
                        "internal readlink failed on %s (%s)",
                        base_str, strerror (errno));
                goto err;
        }

        if (ret < 512)
                linkname[ret] = 0;

        link_len = ret;

        if ((ret == 8) && memcmp (linkname, "../../..", 8) == 0) {
                if (strcmp (base_str, buf) == 0) {
                        strcpy (buf + pfx_len, "..");
                }
                goto out;
        }

        if (ret < 50 || ret >= 512) {
                gf_log (this->name, GF_LOG_ERROR,
                        "malformed internal link %s for %s",
                        linkname, base_str);
                goto err;
        }

        if (memcmp (linkname, "../../", 6) != 0) {
                gf_log (this->name, GF_LOG_ERROR,
                        "malformed internal link %s for %s",
                        linkname, base_str);
                goto err;
        }

        if ((linkname[2] != '/') ||
            (linkname[5] != '/') ||
            (linkname[8] != '/') ||
            (linkname[11] != '/') ||
            (linkname[48] != '/')) {
                gf_log (this->name, GF_LOG_ERROR,
                        "malformed internal link %s for %s",
                        linkname, base_str);
                goto err;
        }

        if ((linkname[20] != '-') ||
            (linkname[25] != '-') ||
            (linkname[30] != '-') ||
            (linkname[35] != '-')) {
                gf_log (this->name, GF_LOG_ERROR,
                        "malformed internal link %s for %s",
                        linkname, base_str);
                goto err;
        }

        blen = link_len - 48;
        memmove (buf + base_len + blen, buf + base_len,
                 (strlen (buf) - base_len) + 1);

        strncpy (base_str + pfx_len, linkname + 6, 42);

        if (len + blen < maxlen)
                strncpy (buf + pfx_len, linkname + 6, link_len - 6);
out:
        return len + blen;
err:
        return -1;
}


/*
  posix_handle_path differs from posix_handle_gfid_path in the way that the
  path filled in @buf by posix_handle_path will return type IA_IFDIR when
  an lstat() is performed on it, whereas posix_handle_gfid_path returns path
  to the handle symlink (typically used for the purpose of unlinking it).

  posix_handle_path also guarantees immunity to ELOOP on the path returned by it
*/

int
posix_handle_path (xlator_t *this, uuid_t gfid, const char *basename,
                   char *ubuf, size_t size)
{
        struct posix_private *priv = NULL;
        char                 *uuid_str = NULL;
        int                   len = 0;
        int                   ret = -1;
        struct stat           stat;
        char                 *base_str = NULL;
        int                   base_len = 0;
        int                   pfx_len;
        int                   maxlen;
        char                 *buf;

        priv = this->private;

        uuid_str = uuid_utoa (gfid);

        if (ubuf) {
                buf = ubuf;
                maxlen = size;
        } else {
                maxlen = PATH_MAX;
                buf = alloca (maxlen);
        }

        base_len = (priv->base_path_length + SLEN(HANDLE_PFX) + 45);
        base_str = alloca (base_len + 1);
        base_len = snprintf (base_str, base_len + 1, "%s/%s/%02x/%02x/%s",
                             priv->base_path, HANDLE_PFX, gfid[0], gfid[1],
                             uuid_str);

        pfx_len = priv->base_path_length + 1 + SLEN(HANDLE_PFX) + 1;

        if (basename) {
                len = snprintf (buf, maxlen, "%s/%s", base_str, basename);
        } else {
                len = snprintf (buf, maxlen, "%s", base_str);
        }

        ret = lstat (base_str, &stat);

        if (!(ret == 0 && S_ISLNK(stat.st_mode) && stat.st_nlink == 1))
                goto out;

        do {
                errno = 0;
                ret = posix_handle_pump (this, buf, len, maxlen,
                                         base_str, base_len, pfx_len);
                if (ret == -1)
                        break;

                len = ret;

                ret = lstat (buf, &stat);
        } while ((ret == -1) && errno == ELOOP);

out:
        return len + 1;
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
                + SLEN("/")
                + SLEN(HANDLE_PFX)
                + SLEN("/")
                + SLEN("00/")
                + SLEN("00/")
                + SLEN(UUID0_STR)
                + 1 /* '\0' */
                ;

        if (basename) {
                len += (strlen (basename) + 1);
        } else {
                len += 256;  /* worst-case for directory's symlink-handle expansion */
        }

        if ((buflen < len) || !buf)
                return len;

        uuid_str = uuid_utoa (gfid);

        if (__is_root_gfid (gfid)) {
                if (basename) {
                        len = snprintf (buf, buflen, "%s/%s", priv->base_path,
                                        basename);
                } else {
                        strncpy (buf, priv->base_path, buflen);
                }
                goto out;
        }

        if (basename) {
                len = snprintf (buf, buflen, "%s/%s/%02x/%02x/%s/%s", priv->base_path,
                                HANDLE_PFX, gfid[0], gfid[1], uuid_str, basename);
        } else {
                len = snprintf (buf, buflen, "%s/%s/%02x/%02x/%s", priv->base_path,
                                HANDLE_PFX, gfid[0], gfid[1], uuid_str);
        }
out:
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

gf_boolean_t
posix_does_old_trash_exists (char *old_trash)
{
        uuid_t                gfid = {0};
        gf_boolean_t          exists = _gf_false;
        struct stat           stbuf = {0};
        int                   ret = 0;

        ret = lstat (old_trash, &stbuf);
        if ((ret == 0) && S_ISDIR (stbuf.st_mode)) {
                ret = sys_lgetxattr (old_trash, "trusted.gfid", gfid, 16);
                if ((ret < 0) && (errno == ENODATA))
                        exists = _gf_true;
        }
        return exists;
}

int
posix_handle_new_trash_init (xlator_t *this, char *trash)
{
        int                     ret = 0;
        struct stat             stbuf = {0};

        ret = lstat (trash, &stbuf);
        switch (ret) {
        case -1:
                if (errno == ENOENT) {
                        ret = mkdir (trash, 0755);
                        if (ret != 0) {
                                gf_log (this->name, GF_LOG_ERROR,
                                        "Creating directory %s failed: %s",
                                        trash, strerror (errno));
                        }
                } else {
                        gf_log (this->name, GF_LOG_ERROR, "Checking for %s "
                                "failed: %s", trash, strerror (errno));
                }
                break;
        case 0:
                if (!S_ISDIR (stbuf.st_mode)) {
                        gf_log (this->name, GF_LOG_ERROR,
                                "Not a directory: %s", trash);
                        ret = -1;
                }
                break;
        default:
                break;
        }
        return ret;
}

int
posix_mv_old_trash_into_new_trash (xlator_t *this, char *old, char *new)
{
        char                  dest_old[PATH_MAX] = {0};
        int                   ret = 0;
        uuid_t                dest_name = {0};

        if (!posix_does_old_trash_exists (old))
                goto out;
        uuid_generate (dest_name);
        snprintf (dest_old, sizeof (dest_old), "%s/%s", new,
                  uuid_utoa (dest_name));
        ret = rename (old, dest_old);
        if (ret < 0) {
                gf_log (this->name, GF_LOG_ERROR, "Not able to move "
                        "%s -> %s (%s)", old, dest_old, strerror (errno));
        }
out:
        return ret;
}

int
posix_handle_trash_init (xlator_t *this)
{
        int                   ret = -1;
        struct posix_private  *priv = NULL;
        char                  old_trash[PATH_MAX] = {0};

        priv = this->private;

        priv->trash_path = GF_CALLOC (1, priv->base_path_length + strlen ("/")
                                      + strlen (HANDLE_PFX) + strlen ("/")
                                      + strlen (TRASH_DIR) + 1,
                                      gf_posix_mt_trash_path);

        if (!priv->trash_path)
                goto out;

        strncpy (priv->trash_path, priv->base_path, priv->base_path_length);
        strcat (priv->trash_path, "/" HANDLE_PFX "/" TRASH_DIR);
        ret = posix_handle_new_trash_init (this, priv->trash_path);
        if (ret)
                goto out;
        snprintf (old_trash, sizeof (old_trash), "%s/.landfill",
                  priv->base_path);
        ret = posix_mv_old_trash_into_new_trash (this, old_trash,
                                                 priv->trash_path);
out:
        return ret;
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

#ifdef HAVE_LINKAT
                /*
                 * Use linkat if the target may be a symlink to a directory
                 * or without an existing target. See comment about linkat()
                 * usage in posix_link() in posix.c for details
                 */
                ret = linkat (AT_FDCWD, oldpath, AT_FDCWD, newpath, 0);
#else
                ret = link (oldpath, newpath);
#endif
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


        if (!basename) {
                ret = posix_handle_unset_gfid (this, gfid);
                return ret;
        }

        MAKE_HANDLE_PATH (path, this, gfid, basename);

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
posix_create_link_if_gfid_exists (xlator_t *this, uuid_t gfid,
                                  char *real_path)
{
        int ret = -1;
        struct stat stbuf = {0,};
        char *newpath = NULL;

        MAKE_HANDLE_PATH (newpath, this, gfid, NULL);
        ret = lstat (newpath, &stbuf);
        if (!ret) {
#ifdef HAVE_LINKAT
                /*
                 * Use linkat if the target may be a symlink to a directory
                 * or without an existing target. See comment about linkat()
                 * usage in posix_link() in posix.c for details
                 */
                ret = linkat (AT_FDCWD, newpath, AT_FDCWD, real_path, 0);
#else
                ret = link (newpath, real_path);
#endif
        }

        return ret;
}
