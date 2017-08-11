/*
  Copyright (c) 2016 Red Hat, Inc. <http://www.redhat.com>
  This file is part of GlusterFS.

  This file is licensed to you under your choice of the GNU Lesser
  General Public License, version 3 or any later version (LGPLv3 or
  later), or the GNU General Public License, version 2 (GPLv2), in all
  cases as published by the Free Software Foundation.
*/

/* File: posix2-common.c
 * This file contains common routines across ds and mds posix xlators
 */

#include <libgen.h>

#include "common-utils.h"
#include "xlator.h"
#include "syscall.h"
#include "posix-messages.h"
#include "posix.h"

#define POSIX2_ENTRY_HANDLE_FMT  "%s/%02x/%02x/%s"

/* NOTE: We do not bother with PATH_MAX as our paths are well defined, and in
the extreme case that a brick path is loooong... is when we will fail, at
which point if we correct this for relative paths, things should go back to
normal */
int
posix2_handle_length (int32_t baselen)
{
        return baselen          /*   basepath length    */
               + 1              /*       /              */
               + 2              /*   GFID[0.1]          */
               + 1              /*       /              */
               + 2              /*   GFID[2.3]          */
               + 1              /*       /              */
               + 36             /*   PARGFID            */
               + 1;             /*       \0             */
}

int
posix2_make_handle (uuid_t gfid, char *basepath, char *handle,
                    size_t handlesize)
{
        return snprintf (handle, handlesize, POSIX2_ENTRY_HANDLE_FMT,
                         basepath, gfid[0], gfid[1], uuid_utoa (gfid));
}

int
posix2_istat_path (xlator_t *this, uuid_t gfid, const char *ipath,
                   struct iatt *buf_p, gf_boolean_t dircheck)
{
        struct stat  lstatbuf = {0, };
        struct iatt  stbuf = {0, };
        int          ret = 0;
        struct posix_private *priv = NULL;

        priv = this->private;

        ret = sys_lstat (ipath, &lstatbuf);

        if (ret != 0) {
                if (ret == -1) {
                        if (errno != ENOENT && errno != ELOOP)
                                gf_msg (this->name, GF_LOG_WARNING, errno,
                                        P_MSG_LSTAT_FAILED,
                                        "lstat failed on %s",
                                        ipath);
                } else {
                        /* may be some backend filesystem issue */
                        gf_msg (this->name, GF_LOG_ERROR, 0, P_MSG_LSTAT_FAILED,
                                "lstat failed on %s and return value is %d "
                                "instead of -1. Please see dmesg output to "
                                "check whether the failure is due to backend "
                                "filesystem issue", ipath, ret);
                        ret = -1;
                }
                goto out;
        }

        if (dircheck && !S_ISDIR (lstatbuf.st_mode)) {
                errno = ENOTDIR;
                goto out;
        }

        if ((lstatbuf.st_ino == priv->handledir.st_ino) &&
            (lstatbuf.st_dev == priv->handledir.st_dev)) {
                errno = ENOENT;
                return -1;
        }

        iatt_from_stat (&stbuf, &lstatbuf);

        gf_uuid_copy (stbuf.ia_gfid, gfid);

        posix_fill_ino_from_gfid (this, &stbuf);

        if (buf_p)
                *buf_p = stbuf;
out:
        return ret;
}

int32_t
posix2_resolve_inode (xlator_t *this, uuid_t tgtuuid, struct iatt *stbuf,
                      gf_boolean_t dircheck)
{
        int entrylen = 0, retlen;
        char *entry = NULL;
        struct posix_private *priv = NULL;

        priv = this->private;

        entrylen = posix2_handle_length (priv->base_path_length);
        entry = alloca (entrylen);

        retlen = posix2_make_handle (tgtuuid, priv->base_path, entry,
                                     entrylen);
        if (entrylen != retlen)
                goto error_return;

        return posix2_istat_path (this, tgtuuid, entry, stbuf, dircheck);

error_return:
        errno = EINVAL;
        return -1;
}

int32_t
posix2_resolve_entry (xlator_t *this, char *parpath, const char *basename,
                      uuid_t gfid)
{
        int size;
        uuid_t egfid;
        char realpath[PATH_MAX] = {0,};

        (void) snprintf (realpath, PATH_MAX, "%s/%s", parpath, basename);
        size = sys_lgetxattr (realpath, GFID_XATTR_KEY, &egfid, sizeof (egfid));
        if (size == -1 || size != sizeof (egfid)) {
                goto error_return;
        }

        gf_uuid_copy (gfid, egfid);
        return 0;
error_return:
        errno = EINVAL;
        return -1;
}

int32_t
posix2_handle_entry (xlator_t *this, char *parpath, const char *basename,
                     struct iatt *stbuf)
{
        int32_t ret = 0;
        uuid_t tgtuuid = {0,};

        ret = posix2_resolve_entry (this, parpath, basename, tgtuuid);
        if (ret)
                goto error_return;

        ret = posix2_resolve_inode (this, tgtuuid, stbuf, _gf_false);
        if (ret < 0) {
                if (errno == ENOENT) {
                        gf_uuid_copy (stbuf->ia_gfid, tgtuuid);
                        errno = EREMOTE;
                }
                goto error_return;
        }

        return 0;

error_return:
        return -1;
}

int32_t
posix2_create_dir_hashes (xlator_t *this, char *entry)
{
        int32_t ret = 0;
        char *duppath = NULL;
        char *parpath = NULL;

        duppath = strdupa (entry);

        /* twice.. so that we get to the end of first dir entry in the path */
        parpath = dirname (duppath);
        parpath = dirname (duppath);

        ret = mkdir (parpath, 0700);
        if ((ret == -1) && (errno != EEXIST)) {
                gf_msg (this->name, GF_LOG_ERROR, errno, 0,
                        "Error creating directory level #1 for [%s]", entry);
                goto error_return;
        }

        strcpy (duppath, entry);
        parpath = dirname (duppath);

        ret = mkdir (parpath, 0700);
        if ((ret == -1) && (errno != EEXIST)) {
                gf_msg (this->name, GF_LOG_ERROR, errno, 0,
                        "Error creating directory level #2 for [%s]", entry);
                goto error_return;
        }

        return 0;

error_return:
        /* no point in rolling back */
        return -1;
}

/**
 * TODO: save separate inodeptr metadata.
 */
int32_t
posix2_create_inode (xlator_t *this, char *entry, int32_t flags, mode_t mode)
{
        int fd = -1;
        int32_t ret = 0;
        gf_boolean_t isdir = S_ISDIR (mode);

        ret = posix2_create_dir_hashes (this, entry);
        if (ret < 0) {
                goto error_return;
        }
        if (isdir) {
                ret = mkdir (entry, mode);
        } else {
                if (!flags)
                        flags = (O_CREAT | O_RDWR | O_EXCL);
                else
                        flags |= O_CREAT;

                fd = open (entry, flags, mode);
                if (fd < 0)
                        ret = -1;
                else
                        sys_close (fd);
        }

error_return:
        return (ret < 0) ? -1 : 0;
}

int32_t
posix2_link_inode (xlator_t *this, char *parpath, const char *basename,
                   uuid_t gfid)
{
        int32_t ret = -1;
        int fd = -1;
        char realpath[PATH_MAX] = {0,};

        (void) snprintf (realpath, PATH_MAX, "%s/%s", parpath, basename);
        fd = open (realpath, O_CREAT | O_EXCL | O_WRONLY, 0700);
        if (fd < 0)
                goto error_return;

        ret = sys_fsetxattr (fd, GFID_XATTR_KEY, gfid, sizeof (*gfid), 0);

        close (fd);

error_return:
        return (ret < 0) ? -1 : 0;
}

int32_t
posix2_save_openfd (xlator_t *this, fd_t *fd, int openfd, int32_t flags)
{
        int32_t ret = 0;
        struct posix_fd *pfd = NULL;

        pfd = GF_CALLOC (1, sizeof (*pfd), gf_posix_mt_posix_fd);
        if (!pfd)
                return -1;

        pfd->fd = openfd;
        pfd->flags = flags;

/* TODO: Bring this in if directory open (in opendir needs handling)
        if (flags & O_DIRECTORY) {
                pfd->dirfd = fdopendir (openfd);
                if (pfd->dirfd == NULL) {
                        GF_FREE (pfd);
                        return -1;
                }
        }*/

        ret = fd_ctx_set (fd, this, (uint64_t)(long)pfd);
        if (ret) {
                GF_FREE (pfd);
                pfd = NULL;
        }
        return ret;
}

int32_t
posix2_lookup_is_nameless (loc_t *loc)
{
        return (gf_uuid_is_null (loc->pargfid) && !loc->name);
}

void
posix2_fill_ino_from_gfid (xlator_t *this, struct iatt *buf)
{
        uint64_t temp_ino = 0;
        int j = 0;
        int i = 0;

        /* consider least significant 8 bytes of value out of gfid */
        if (gf_uuid_is_null (buf->ia_gfid)) {
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
