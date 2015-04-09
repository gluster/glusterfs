/*
  Copyright (c) 2015 Red Hat, Inc. <http://www.redhat.com>
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

#include "syncop.h"
#include "common-utils.h"

int
syncop_dirfd (xlator_t *subvol, loc_t *loc, fd_t **fd, int pid)
{
        int  ret    = 0;
        fd_t *dirfd = NULL;

        if (!fd)
                return -EINVAL;

        dirfd = fd_create (loc->inode, pid);
        if (!dirfd) {
                gf_log (subvol->name, GF_LOG_ERROR,
                        "fd_create of %s failed: %s",
                        uuid_utoa (loc->gfid), strerror(errno));
                ret = -errno;
                goto out;
        }

        ret = syncop_opendir (subvol, loc, dirfd, NULL, NULL);
        if (ret) {
        /*
         * On Linux, if the brick was not updated, opendir will
         * fail. We therefore use backward compatible code
         * that violate the standards by reusing offsets
         * in seekdir() from different DIR *, but it works on Linux.
         *
         * On other systems it never worked, hence we do not need
         * to provide backward-compatibility.
         */
#ifdef GF_LINUX_HOST_OS
                fd_unref (dirfd);
                dirfd = fd_anonymous (loc->inode);
                if (!dirfd) {
                        gf_log(subvol->name, GF_LOG_ERROR,
                               "fd_anonymous of %s failed: %s",
                               uuid_utoa (loc->gfid), strerror(errno));
                        ret = -errno;
                        goto out;
                }
                ret = 0;
#else /* GF_LINUX_HOST_OS */
                fd_unref (dirfd);
                gf_log (subvol->name, GF_LOG_ERROR,
                        "opendir of %s failed: %s",
                        uuid_utoa (loc->gfid), strerror(errno));
                goto out;
#endif /* GF_LINUX_HOST_OS */
        }
out:
        if (ret == 0)
                *fd = dirfd;
        return ret;
}

int
syncop_ftw (xlator_t *subvol, loc_t *loc, int pid, void *data,
            int (*fn) (xlator_t *subvol, gf_dirent_t *entry, loc_t *parent,
                       void *data))
{
        loc_t       child_loc = {0, };
        fd_t        *fd       = NULL;
        uint64_t    offset    = 0;
        gf_dirent_t *entry    = NULL;
        int         ret       = 0;
        gf_dirent_t entries;

        ret = syncop_dirfd (subvol, loc, &fd, pid);
        if (ret)
                goto out;

        INIT_LIST_HEAD (&entries.list);

        while ((ret = syncop_readdirp (subvol, fd, 131072, offset, &entries,
                                       NULL, NULL))) {
                if (ret < 0)
                        break;

                if (ret > 0) {
                        /* If the entries are only '.', and '..' then ret
                         * value will be non-zero. so set it to zero here. */
                        ret = 0;
                }
                list_for_each_entry (entry, &entries.list, list) {
                        offset = entry->d_off;

                        if (!strcmp (entry->d_name, ".") ||
                            !strcmp (entry->d_name, ".."))
                                continue;

                        gf_link_inode_from_dirent (NULL, fd->inode, entry);

                        ret = fn (subvol, entry, loc, data);
                        if (ret)
                                break;

                        if (entry->d_stat.ia_type == IA_IFDIR) {
                                child_loc.inode = inode_ref (entry->inode);
                                gf_uuid_copy (child_loc.gfid, entry->inode->gfid);
                                ret = syncop_ftw (subvol, &child_loc,
                                                  pid, data, fn);
                                loc_wipe (&child_loc);
                                if (ret)
                                        break;
                        }
                }

                gf_dirent_free (&entries);
                if (ret)
                        break;
        }

out:
        if (fd)
                fd_unref (fd);
        return ret;
}

/**
 * Syncop_ftw_throttle can be used in a configurable way to control
 * the speed at which crawling is done. It takes 2 more arguments
 * compared to syncop_ftw.
 * After @count entries are finished in a directory (to be
 * precise, @count files) sleep for @sleep_time seconds.
 * If either @count or @sleep_time is <=0, then it behaves similar to
 * syncop_ftw.
 */
int
syncop_ftw_throttle (xlator_t *subvol, loc_t *loc, int pid, void *data,
                     int (*fn) (xlator_t *subvol, gf_dirent_t *entry,
                                loc_t *parent, void *data),
                     int count, int sleep_time)
{
        loc_t       child_loc = {0, };
        fd_t        *fd       = NULL;
        uint64_t    offset    = 0;
        gf_dirent_t *entry    = NULL;
        int         ret       = 0;
        gf_dirent_t entries;
        int         tmp       = 0;

        if (sleep_time <= 0) {
                ret = syncop_ftw (subvol, loc, pid, data, fn);
                goto out;
        }

        ret = syncop_dirfd (subvol, loc, &fd, pid);
        if (ret)
                goto out;

        INIT_LIST_HEAD (&entries.list);

        while ((ret = syncop_readdirp (subvol, fd, 131072, offset, &entries,
                                       NULL, NULL))) {
                if (ret < 0)
                        break;

                if (ret > 0) {
                        /* If the entries are only '.', and '..' then ret
                         * value will be non-zero. so set it to zero here. */
                        ret = 0;
                }

                tmp = 0;

                list_for_each_entry (entry, &entries.list, list) {
                        offset = entry->d_off;

                        if (!strcmp (entry->d_name, ".") ||
                            !strcmp (entry->d_name, ".."))
                                continue;

                        if (++tmp >= count)
                                sleep (sleep_time);

                        gf_link_inode_from_dirent (NULL, fd->inode, entry);

                        ret = fn (subvol, entry, loc, data);
                        if (ret)
                                continue;

                        if (entry->d_stat.ia_type == IA_IFDIR) {
                                child_loc.inode = inode_ref (entry->inode);
                                gf_uuid_copy (child_loc.gfid, entry->inode->gfid);
                                ret = syncop_ftw_throttle (subvol, &child_loc,
                                                           pid, data, fn, count,
                                                           sleep_time);
                                loc_wipe (&child_loc);
                                if (ret)
                                        continue;
                        }
                }

                gf_dirent_free (&entries);
                if (ret)
                        break;
        }

out:
        if (fd)
                fd_unref (fd);
        return ret;
}

int
syncop_dir_scan (xlator_t *subvol, loc_t *loc, int pid, void *data,
                 int (*fn) (xlator_t *subvol, gf_dirent_t *entry, loc_t *parent,
                            void *data))
{
        fd_t        *fd    = NULL;
        uint64_t    offset = 0;
        gf_dirent_t *entry = NULL;
        int         ret    = 0;
        gf_dirent_t entries;

        ret = syncop_dirfd (subvol, loc, &fd, pid);
        if (ret)
                goto out;

        INIT_LIST_HEAD (&entries.list);

        while ((ret = syncop_readdir (subvol, fd, 131072, offset, &entries,
                                      NULL, NULL))) {
                if (ret < 0)
                        break;

                if (ret > 0) {
                        /* If the entries are only '.', and '..' then ret
                         * value will be non-zero. so set it to zero here. */
                        ret = 0;
                }

                list_for_each_entry (entry, &entries.list, list) {
                        offset = entry->d_off;

                        if (!strcmp (entry->d_name, ".") ||
                            !strcmp (entry->d_name, ".."))
                                continue;

                        ret = fn (subvol, entry, loc, data);
                        if (ret)
                                break;
                }
                gf_dirent_free (&entries);
                if (ret)
                        break;
        }

out:
        if (fd)
                fd_unref (fd);
        return ret;
}

int
syncop_is_subvol_local (xlator_t *this, loc_t *loc, gf_boolean_t *is_local)
{
        char *pathinfo = NULL;
        dict_t *xattr = NULL;
        int ret = 0;

        if (!this || !this->type || !is_local)
                return -EINVAL;

        if (strcmp (this->type, "protocol/client") != 0)
                return -EINVAL;

        *is_local = _gf_false;

        ret = syncop_getxattr (this, loc, &xattr, GF_XATTR_PATHINFO_KEY, NULL,
                               NULL);
        if (ret < 0) {
                ret = -1;
                goto out;
        }

        if (!xattr) {
                ret = -EINVAL;
                goto out;
        }

        ret = dict_get_str (xattr, GF_XATTR_PATHINFO_KEY, &pathinfo);
        if (ret)
                goto out;

        ret = glusterfs_is_local_pathinfo (pathinfo, is_local);

        gf_log (this->name, GF_LOG_DEBUG, "subvol %s is %slocal",
                this->name, is_local ? "" : "not ");

out:
        if (xattr)
                dict_unref (xattr);

        return ret;
}

int
syncop_gfid_to_path (inode_table_t *itable, xlator_t *subvol, uuid_t gfid,
                     char **path_p)
{
        int      ret   = 0;
        char    *path  = NULL;
        loc_t    loc   = {0,};
        dict_t  *xattr = NULL;

        gf_uuid_copy (loc.gfid, gfid);
        loc.inode = inode_new (itable);

        ret = syncop_getxattr (subvol, &loc, &xattr, GFID_TO_PATH_KEY, NULL,
                               NULL);
        if (ret < 0)
                goto out;

        ret = dict_get_str (xattr, GFID_TO_PATH_KEY, &path);
        if (ret || !path) {
                ret = -EINVAL;
                goto out;
        }

        if (path_p) {
                *path_p = gf_strdup (path);
                if (!*path_p) {
                        ret = -ENOMEM;
                        goto out;
                }
        }

        ret = 0;

out:
        if (xattr)
                dict_unref (xattr);
        loc_wipe (&loc);

        return ret;
}
