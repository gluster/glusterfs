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

int
glfsh_get_index_dir_loc (loc_t *rootloc, xlator_t *xl, loc_t *dirloc,
                         int32_t *op_errno)
{
        void      *index_gfid = NULL;
        int       ret = 0;
        dict_t    *xattr = NULL;
        struct iatt   iattr = {0};
        struct iatt   parent = {0};

        ret = syncop_getxattr (xl, rootloc, &xattr, GF_XATTROP_INDEX_GFID);
        if (ret < 0) {
                *op_errno = errno;
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
                *op_errno = errno;
                goto out;
        }
        ret = glfsh_link_inode_update_loc (dirloc, &iattr);
        if (ret)
                goto out;
        glfs_loc_touchup (dirloc);

        return 0;
out:
        if (xattr)
                dict_unref (xattr);
        return ret;
}

static int
_set_self_heal_vxattrs (dict_t *xattr_req)
{
        int     ret = 0;

        ret = dict_set_int32 (xattr_req, "dry-run-self-heal", 1);
        if (ret < 0)
                goto out;

        ret = dict_set_int32 (xattr_req, "attempt-self-heal", 1);
        if (ret < 0)
                goto out;

        ret = dict_set_int32 (xattr_req, "foreground-self-heal", 1);
        if (ret < 0)
                goto out;
out:
        return ret;
}

static gf_boolean_t
_is_self_heal_pending (dict_t *xattr_rsp)
{
        int     ret = 0;
        int     sh_pending = 0;

        ret = dict_get_int32 (xattr_rsp, "metadata-self-heal-pending",
                              &sh_pending);
        if ((ret == 0) && sh_pending) {
                return _gf_true;
        }

        ret = dict_get_int32 (xattr_rsp, "entry-self-heal-pending",
                              &sh_pending);
        if ((ret == 0) && sh_pending) {
                return _gf_true;
        }

        ret = dict_get_int32 (xattr_rsp, "data-self-heal-pending",
                              &sh_pending);
        if ((ret == 0) && sh_pending) {
                return _gf_true;
        }
        return _gf_false;
}

static gf_boolean_t
_is_possibly_healing (dict_t *xattr_rsp)
{
        int     ret = 0;
        int     healing = 0;

        ret = dict_get_int32 (xattr_rsp, "possibly-healing", &healing);
        if ((ret == 0) && healing) {
                return _gf_true;
        }

        return _gf_false;
}

#define RESET_ENTRIES(loc, shf, ope, rsp, grsp) \
        do {                                    \
                loc_wipe (&loc);                \
                shf = 0;                        \
                ope = 0;                        \
                if (rsp) {                      \
                        dict_unref (rsp);       \
                        rsp = NULL;             \
                }                               \
                if (grsp) {                     \
                        dict_unref (grsp);      \
                        grsp = NULL;            \
                }                               \
        } while (0)

static int
glfsh_build_index_loc (loc_t *loc, char *name, loc_t *parent)
{
        int             ret = 0;

        uuid_copy (loc->pargfid, parent->inode->gfid);
        loc->path = "";
        loc->name = name;
        loc->parent = inode_ref (parent->inode);
        if (!loc->parent) {
                loc->path = NULL;
                loc_wipe (loc);
                ret = -1;
        }
        return ret;
}

void
glfsh_remove_stale_index (xlator_t *xl, loc_t *parent, char *fname)
{
        int              ret = 0;
        loc_t            index_loc = {0};

        ret = glfsh_build_index_loc (&index_loc, fname, parent);
        if (ret)
                goto out;
        ret = syncop_unlink (xl, &index_loc);
        index_loc.path = NULL;
        loc_wipe (&index_loc);
out:
        return;
}
static int
glfsh_process_entries (xlator_t *xl, loc_t *parentloc, gf_dirent_t *entries,
                       off_t *offset, uint64_t *num_entries)
{
        gf_dirent_t      *entry = NULL;
        gf_dirent_t      *tmp = NULL;
        int              ret = 0;
        loc_t            entry_loc = {0};
        struct iatt      iattr = {0};
        struct iatt      parent = {0};
        dict_t          *xattr_req = NULL;
        dict_t          *xattr_rsp = NULL;
        dict_t          *getxattr_rsp = NULL;
        int32_t         sh_failed = 0;
        gf_boolean_t    sh_pending = _gf_false;
        char            *path = NULL;
        int32_t         op_errno = 0;

        xattr_req = dict_new ();
        if (!xattr_req)
                goto out;

        list_for_each_entry_safe (entry, tmp, &entries->list, list) {
                *offset = entry->d_off;
                if ((strcmp (entry->d_name, ".") == 0) ||
                    (strcmp (entry->d_name, "..") == 0))
                        continue;

                RESET_ENTRIES (entry_loc, sh_failed, op_errno, xattr_rsp,
                               getxattr_rsp);

                ret = _set_self_heal_vxattrs (xattr_req);
                if (ret < 0)
                        goto out;

                entry_loc.inode = inode_new (parentloc->inode->table);
                if (!entry_loc.inode)
                        goto out;

                uuid_parse (entry->d_name, entry_loc.gfid);
                entry_loc.path = gf_strdup (uuid_utoa (entry_loc.gfid));
                ret = syncop_lookup (xl->parents->xlator, &entry_loc, xattr_req,
                                     &iattr, &xattr_rsp, &parent);
                if (ret < 0)
                        op_errno = errno;

                if (op_errno == ENOENT || op_errno == ESTALE) {
                        glfsh_remove_stale_index (xl, parentloc, entry->d_name);
                        continue;
                }

                ret = dict_get_int32 (xattr_rsp, "sh-failed", &sh_failed);

                sh_pending = _is_self_heal_pending (xattr_rsp);
                //File/dir is undergoing I/O
                if (!op_errno && !sh_failed && !sh_pending)
                        continue;

                ret = syncop_getxattr (xl, &entry_loc, &getxattr_rsp,
                                       GFID_TO_PATH_KEY);

                ret = dict_get_str (getxattr_rsp, GFID_TO_PATH_KEY, &path);


                (*num_entries)++;
                if (_is_possibly_healing (xattr_rsp)) {
                        printf ("%s - Possibly undergoing heal\n",
                                path ? path : uuid_utoa (entry_loc.gfid));
                } else {
                        printf ("%s\n", path ? path : uuid_utoa (entry_loc.gfid));
                }
        }
        ret = 0;
out:
        RESET_ENTRIES (entry_loc, sh_failed, op_errno, xattr_rsp, getxattr_rsp);
        return ret;
}

static int
glfsh_crawl_directory (xlator_t   *readdir_xl, fd_t *fd, loc_t *loc)
{
        off_t           offset   = 0;
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

                ret = glfsh_process_entries (readdir_xl, loc, &entries, &offset,
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
        } else {
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

        ret = syncop_getxattr (xl, rootloc, &xattr, GF_XATTR_PATHINFO_KEY);
        if (ret < 0) {
                ret = -errno;
                goto out;
        }

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
        if (ret < 0) {
                printf ("Brick - Not able to get brick information\n");
        } else {
                printf ("Brick %s:%s\n", remote_host, remote_subvol);
        }
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
                goto out;
        }

        fd = fd_anonymous (dirloc.inode);
        ret = glfsh_crawl_directory (xl, fd, &dirloc);
        if (fd)
                fd_unref (fd);
        if (ret < 0)
                printf ("Failed to find entries with pending self-heal\n");
out:
        loc_wipe (&dirloc);
        return;
}

static gf_boolean_t
_is_afr_an_ancestor (xlator_t *xl)
{
        xlator_t *parent = NULL;

        if (!xl->parents)
                return _gf_false;

        for (parent = xl->parents->xlator; parent->parents;
             parent = parent->parents->xlator) {
                if (!strcmp (parent->type, "cluster/replicate"))
                        return _gf_true;
        }

        return _gf_false;
}

static int
glfsh_validate_replicate_volume (xlator_t *xl)
{
        while (xl->next)
                xl = xl->next;

        while (xl) {
                /* Check if atleast one client xlator has AFR in its parent
                 * ancestry */
                if (!strcmp (xl->type, "protocol/client")) {
                        if (_is_afr_an_ancestor(xl)) {
                                return 1;
                        }
                }
                xl = xl->prev;
        }

        return -1;
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
        char      logfilepath[PATH_MAX];

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
                } else {
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
                } else {
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
                        if (_is_afr_an_ancestor (xl)) {
                                glfsh_print_pending_heals (xl, &rootloc);
                                printf("\n");
                        }
                }

                xl = xl->prev;
        }

        loc_wipe (&rootloc);
        //Calling this sometimes gives log messages on stderr.
        //There is no graceful way of disabling that at the moment,
        //since this process dies anyway, ignoring cleanup for now.
        //glfs_fini (fs);

        return 0;
out:
        if (fs && top_subvol)
                glfs_subvol_done (fs, top_subvol);
        loc_wipe (&rootloc);
        //if (fs)
        //        glfs_fini (fs);

        return ret;
}
