/*
 Copyright (c) 2014 Red Hat, Inc. <http://www.redhat.com>
 This file is part of GlusterFS.

 This file is licensed to you under your choice of the GNU Lesser
 General Public License, version 3 or any later version (LGPLv3 or
 later), or the GNU General Public License, version 2 (GPLv2), in all
 cases as published by the Free Software Foundation.
*/

#include <stdio.h>
#include <stdlib.h>
#include <errno.h>
#include "glfs.h"
#include "glfs-handles.h"
#include "glfs-internal.h"
#include "syncop.h"
#include <string.h>
#include <time.h>

#define DEFAULT_HEAL_LOG_FILE_DIRECTORY DATADIR "/log/glusterfs"

int
glfsh_link_inode_update_loc (loc_t *loc, struct iatt *iattr)
{
        inode_t       *link_inode = NULL;
        int           ret = -1;

        link_inode = inode_link (loc->inode, NULL, NULL, iattr);
        if (link_inode == NULL)
                goto out;

        inode_unref (loc->inode);
        loc->inode = link_inode;
        ret = 0;
out:
        return ret;
}

extern int glfs_loc_touchup (loc_t *);
xlator_t *glfs_active_subvol (struct glfs *);
void glfs_subvol_done (struct glfs *, xlator_t *);

int
glfsh_get_index_dir_loc (loc_t *rootloc, xlator_t *xl, loc_t *dirloc,
                         int32_t *op_errno)
{
        void      *index_gfid = NULL;
        int       ret = 0;
        dict_t    *xattr = NULL;
        struct iatt   iattr = {0};
        struct iatt   parent = {0};

        ret = syncop_getxattr (xl, rootloc, &xattr, GF_XATTROP_INDEX_GFID,
                               NULL);
        if (ret < 0) {
                *op_errno = -ret;
                goto out;
        }

        ret = dict_get_ptr (xattr, GF_XATTROP_INDEX_GFID, &index_gfid);
        if (ret < 0) {
                *op_errno = EINVAL;
                goto out;
        }

        uuid_copy (dirloc->gfid, index_gfid);
        dirloc->path = "";
        dirloc->inode = inode_new (rootloc->inode->table);
        ret = syncop_lookup (xl, dirloc, NULL,
                             &iattr, NULL, &parent);
        dirloc->path = NULL;
        if (ret < 0) {
                *op_errno = -ret;
                goto out;
        }
        ret = glfsh_link_inode_update_loc (dirloc, &iattr);
        if (ret)
                goto out;
        glfs_loc_touchup (dirloc);

        ret = 0;
out:
        if (xattr)
                dict_unref (xattr);
        return ret;
}

static xlator_t*
_get_afr_ancestor (xlator_t *xl)
{
        if (!xl || !xl->parents)
                return NULL;

        while (xl->parents) {
                xl = xl->parents->xlator;
                if (!xl)
                        break;
                if (strcmp (xl->type, "cluster/replicate") == 0)
                        return xl;
        }

        return NULL;
}

int
glfsh_index_purge (xlator_t *subvol, inode_t *inode, char *name)
{
        loc_t loc = {0, };
        int ret = 0;

        loc.parent = inode_ref (inode);
        loc.name = name;

        ret = syncop_unlink (subvol, &loc);

        loc_wipe (&loc);
        return ret;
}

int
glfsh_gfid_to_path (xlator_t *this, xlator_t *subvol, uuid_t gfid, char **path_p)
{
        int      ret   = 0;
        char    *path  = NULL;
        loc_t    loc   = {0,};
        dict_t  *xattr = NULL;

        uuid_copy (loc.gfid, gfid);
        loc.inode = inode_new (this->itable);

        ret = syncop_getxattr (subvol, &loc, &xattr, GFID_TO_PATH_KEY, NULL);
        if (ret)
                goto out;

        ret = dict_get_str (xattr, GFID_TO_PATH_KEY, &path);
        if (ret || !path) {
                ret = -EINVAL;
                goto out;
        }

        *path_p = gf_strdup (path);
        if (!*path_p) {
                ret = -ENOMEM;
                goto out;
        }

        ret = 0;

out:
        if (xattr)
                dict_unref (xattr);
        loc_wipe (&loc);

        return ret;
}

void
glfsh_print_heal_status (dict_t *dict, char *path, uuid_t gfid,
                         uint64_t *num_entries)
{
       char *value  = NULL;
       int   ret    = 0;
       char *status = NULL;

       ret = dict_get_str (dict, "heal-info", &value);
       if (ret || (!strcmp (value, "no-heal")))
               return;

       (*num_entries)++;
       if (!strcmp (value, "heal")) {
               ret = gf_asprintf (&status, " ");
       } else if (!strcmp (value, "possibly-healing")) {
               ret = gf_asprintf (&status, " - Possibly undergoing heal\n");
       } else if (!strcmp (value, "split-brain")) {
               ret = gf_asprintf (&status, " - Is in split-brain\n");
       }
       if (ret == -1)
               status = NULL;

       printf ("%s%s\n",
               path ? path : uuid_utoa (gfid),
               status);

       if (status)
               GF_FREE (status);
       return;
}

static int
glfsh_process_entries (xlator_t *xl, fd_t *fd, gf_dirent_t *entries,
                       uint64_t *offset, uint64_t *num_entries)
{
        gf_dirent_t      *entry = NULL;
        gf_dirent_t      *tmp = NULL;
        int              ret = 0;
        char            *path = NULL;
        uuid_t          gfid = {0};
        xlator_t        *this = NULL;
        dict_t          *dict = NULL;
        loc_t           loc   = {0,};
        this = THIS;

        list_for_each_entry_safe (entry, tmp, &entries->list, list) {
                *offset = entry->d_off;
                if ((strcmp (entry->d_name, ".") == 0) ||
                    (strcmp (entry->d_name, "..") == 0))
                        continue;

                if (dict) {
                        dict_unref (dict);
                        dict = NULL;
                }
                uuid_clear (gfid);
                GF_FREE (path);
                path = NULL;

                uuid_parse (entry->d_name, gfid);
                uuid_copy (loc.gfid, gfid);
                ret = syncop_getxattr (this, &loc, &dict, GF_AFR_HEAL_INFO,
                                       NULL);
                if (ret)
                        continue;

                ret = glfsh_gfid_to_path (this, xl, gfid, &path);

                if (ret == -ENOENT || ret == -ESTALE) {
                        glfsh_index_purge (xl, fd->inode, entry->d_name);
                        ret = 0;
                        continue;
                }
                if (dict)
                        glfsh_print_heal_status (dict, path, gfid,
                                                 num_entries);
        }
        ret = 0;
        GF_FREE (path);
        if (dict) {
                dict_unref (dict);
                dict = NULL;
        }
        return ret;
}

static int
glfsh_crawl_directory (xlator_t   *readdir_xl, fd_t *fd, loc_t *loc)
{
        uint64_t        offset = 0;
        gf_dirent_t     entries;
        int             ret = 0;
        gf_boolean_t    free_entries = _gf_false;
        uint64_t        num_entries = 0;

        INIT_LIST_HEAD (&entries.list);

        while (1) {
                ret = syncop_readdir (readdir_xl, fd, 131072, offset, &entries);
                if (ret <= 0)
                        break;
                ret = 0;
                free_entries = _gf_true;

                if (list_empty (&entries.list))
                        goto out;

                ret = glfsh_process_entries (readdir_xl, fd, &entries, &offset,
                                             &num_entries);
                if (ret < 0)
                        goto out;

                gf_dirent_free (&entries);
                free_entries = _gf_false;
        }
        ret = 0;
out:
        if (free_entries)
                gf_dirent_free (&entries);
        if (ret < 0) {
                printf ("Failed to complete gathering info. "
                         "Number of entries so far: %"PRIu64"\n", num_entries);
        }
        else {
                printf ("Number of entries: %"PRIu64"\n", num_entries);
        }
        return ret;
}

static int
glfsh_print_brick (xlator_t *xl, loc_t *rootloc)
{
        int     ret = 0;
        dict_t  *xattr = NULL;
        char    *pathinfo = NULL;
        char    *brick_start = NULL;
        char    *brick_end = NULL;

        ret = syncop_getxattr (xl, rootloc, &xattr, GF_XATTR_PATHINFO_KEY,
                               NULL);
        if (ret < 0)
                goto out;

        ret = dict_get_str (xattr, GF_XATTR_PATHINFO_KEY, &pathinfo);
        if (ret < 0)
                goto out;

        brick_start = strchr (pathinfo, ':') + 1;
        brick_end = pathinfo + strlen (pathinfo) - 1;
        *brick_end = 0;
        printf ("Brick %s\n", brick_start);

out:
        if (xattr)
                dict_unref (xattr);
        return ret;
}

void
glfsh_print_brick_from_xl (xlator_t *xl)
{
        char    *remote_host = NULL;
        char    *remote_subvol = NULL;
        int     ret = 0;

        ret = dict_get_str (xl->options, "remote-host", &remote_host);
        if (ret < 0)
                goto out;

        ret = dict_get_str (xl->options, "remote-subvolume", &remote_subvol);
        if (ret < 0)
                goto out;
out:
        if (ret < 0)
                printf ("Brick - Not able to get brick information\n");
        else
                printf ("Brick %s:%s\n", remote_host, remote_subvol);
}

void
glfsh_print_pending_heals (xlator_t *xl, loc_t *rootloc)
{
        int ret = 0;
        loc_t   dirloc = {0};
        fd_t    *fd = NULL;
        int32_t op_errno = 0;

        ret = glfsh_print_brick (xl, rootloc);
        if (ret < 0) {
                glfsh_print_brick_from_xl (xl);
                printf ("Status: %s\n", strerror (-ret));
                goto out;
        }

        ret = glfsh_get_index_dir_loc (rootloc, xl, &dirloc, &op_errno);
        if (ret < 0) {
                if (op_errno == ESTALE || op_errno == ENOENT)
                        printf ("Number of entries: 0\n");
                else
                        printf ("Status: %s\n", strerror (op_errno));
                goto out;
        }

        fd = fd_create (dirloc.inode, GF_CLIENT_PID_GLFS_HEAL);
        if (!fd) {
                printf ("fd_create failed: %s", strerror(errno));
                goto out;
        }
        ret = syncop_opendir (xl, &dirloc, fd);
        if (ret) {
                fd_unref(fd);
#ifdef GF_LINUX_HOST_OS /* See comment in afr_shd_index_opendir() */
                fd = fd_anonymous (dirloc.inode);
                if (!fd) {
                        printf ("fd_anonymous failed: %s",
                                strerror(errno));
                        goto out;
                }
#else
                printf ("opendir failed: %s", strerror(errno));
                goto out;
#endif
        }

        ret = glfsh_crawl_directory (xl, fd, &dirloc);
        if (fd)
                fd_unref (fd);
        if (ret < 0)
                printf ("Failed to find entries with pending self-heal\n");
out:
        loc_wipe (&dirloc);
        return;
}

static int
glfsh_validate_replicate_volume (xlator_t *xl)
{
        xlator_t        *afr_xl = NULL;
        int             ret = -1;

        while (xl->next)
                xl = xl->next;

        while (xl) {
                if (strcmp (xl->type, "protocol/client") == 0) {
                        afr_xl = _get_afr_ancestor (xl);
                        if (afr_xl) {
                                ret = 0;
                                break;
                        }
                }

                xl = xl->prev;
        }

        return ret;
}

int
main (int argc, char **argv)
{
        glfs_t    *fs = NULL;
        int        ret = 0;
        char      *volname = NULL;
        xlator_t  *top_subvol = NULL;
        xlator_t  *xl = NULL;
        loc_t     rootloc = {0};
        char      logfilepath[PATH_MAX] = {0};
        xlator_t  *old_THIS = NULL;
        xlator_t  *afr_xl = NULL;

        if (argc != 2) {
                printf ("Usage: %s <volname>\n", argv[0]);
                ret = -1;
                goto out;
        }
        volname = argv[1];

        fs = glfs_new (volname);
        if (!fs) {
                ret = -1;
                printf ("Not able to initialize volume '%s'\n", volname);
                goto out;
        }

        ret = glfs_set_volfile_server (fs, "tcp", "localhost", 24007);
        snprintf (logfilepath, sizeof (logfilepath),
                  DEFAULT_HEAL_LOG_FILE_DIRECTORY"/glfsheal-%s.log", volname);
        ret = glfs_set_logging(fs, logfilepath, GF_LOG_INFO);
        if (ret < 0) {
                ret = -1;
                printf ("Not able to initialize volume '%s'\n", volname);
                goto out;
        }

        ret = glfs_init (fs);
        if (ret < 0) {
                ret = -1;
                if (errno == ENOENT) {
                        printf ("Volume %s does not exist\n", volname);
                }
                else {
                        printf ("%s: Not able to fetch volfile from "
                                 "glusterd\n", volname);
                }
                goto out;
        }

        sleep (2);
        __glfs_entry_fs (fs);
        top_subvol = glfs_active_subvol (fs);
        if (!top_subvol) {
                ret = -1;
                if (errno == ENOTCONN) {
                        printf ("Volume %s is not started (Or) All the bricks "
                                 "are not running.\n", volname);
                }
                else {
                        printf ("%s: Not able to mount the volume, %s\n",
                                 volname, strerror (errno));
                }
                goto out;
        }

        ret = glfsh_validate_replicate_volume (top_subvol);
        if (ret < 0) {
                printf ("Volume %s is not of type replicate\n", volname);
                goto out;
        }
        rootloc.inode = inode_ref (top_subvol->itable->root);
        glfs_loc_touchup (&rootloc);

        xl = top_subvol;
        while (xl->next)
                xl = xl->next;

        while (xl) {
                if (strcmp (xl->type, "protocol/client") == 0) {
                        afr_xl = _get_afr_ancestor (xl);
                        if (afr_xl) {
                                old_THIS = THIS;
                                THIS = afr_xl;
                                glfsh_print_pending_heals (xl, &rootloc);
                                THIS = old_THIS;
                                printf("\n");
                        }
                }

                xl = xl->prev;
        }

        loc_wipe (&rootloc);
        glfs_subvol_done (fs, top_subvol);
        glfs_fini (fs);

        return 0;
out:
        if (fs && top_subvol)
                glfs_subvol_done (fs, top_subvol);
        loc_wipe (&rootloc);
        if (fs)
                glfs_fini (fs);

        return ret;
}
