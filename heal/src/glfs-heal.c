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
#include "protocol-common.h"
#include "syncop.h"
#include "syncop-utils.h"
#include <string.h>
#include <time.h>

#define DEFAULT_HEAL_LOG_FILE_DIRECTORY DATADIR "/log/glusterfs"
#define USAGE_STR "Usage: %s <VOLNAME> [bigger-file <FILE> | "\
                  "source-brick <HOSTNAME:BRICKNAME> [<FILE>] | "\
                  "split-brain-info]\n"

typedef void    (*print_status) (dict_t *, char *, uuid_t, uint64_t *);

int glfsh_heal_splitbrain_file (glfs_t *fs, xlator_t *top_subvol,
                                loc_t *rootloc, char *file, dict_t *xattr_req);

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

void
glfsh_print_spb_status (dict_t *dict, char *path, uuid_t gfid,
                        uint64_t *num_entries)
{
       char *value  = NULL;
       int   ret    = 0;

       ret = dict_get_str (dict, "heal-info", &value);
       if (ret)
               return;

       if (!strcmp (value, "split-brain")) {
                (*num_entries)++;
                printf ("%s\n",
                        path ? path : uuid_utoa (gfid));
       }
       return;
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
glfsh_heal_entries (glfs_t *fs, xlator_t *top_subvol, loc_t *rootloc,
                    gf_dirent_t *entries,  uint64_t *offset,
                    uint64_t *num_entries, dict_t *xattr_req) {

        gf_dirent_t      *entry          = NULL;
        gf_dirent_t      *tmp            = NULL;
        int               ret            = 0;
        char              file[64]      = {0};

        list_for_each_entry_safe (entry, tmp, &entries->list, list) {
                *offset = entry->d_off;
                if ((strcmp (entry->d_name, ".") == 0) ||
                    (strcmp (entry->d_name, "..") == 0))
                        continue;
                memset (file, 0, sizeof(file));
                snprintf (file, sizeof(file), "gfid:%s", entry->d_name);
                ret = glfsh_heal_splitbrain_file (fs, top_subvol, rootloc, file,
                                                 xattr_req);
                if (ret)
                        continue;
                (*num_entries)++;
        }

        return ret;
}

static int
glfsh_process_entries (xlator_t *xl, fd_t *fd, gf_dirent_t *entries,
                       uint64_t *offset, uint64_t *num_entries,
                       print_status glfsh_print_status)
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

                ret = syncop_gfid_to_path (this->itable, xl, gfid, &path);

                if (ret == -ENOENT || ret == -ESTALE) {
                        glfsh_index_purge (xl, fd->inode, entry->d_name);
                        ret = 0;
                        continue;
                }
                if (dict)
                        glfsh_print_status (dict, path, gfid,
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
glfsh_crawl_directory (glfs_t *fs, xlator_t *top_subvol, loc_t *rootloc,
                       xlator_t *readdir_xl, fd_t *fd, loc_t *loc,
                       dict_t *xattr_req)
{
        uint64_t        offset = 0;
        gf_dirent_t     entries;
        int             ret = 0;
        gf_boolean_t    free_entries = _gf_false;
        uint64_t        num_entries = 0;
        int             heal_op = -1;

        INIT_LIST_HEAD (&entries.list);
        ret = dict_get_int32 (xattr_req, "heal-op", &heal_op);
        if (ret)
                return ret;

        while (1) {
                ret = syncop_readdir (readdir_xl, fd, 131072, offset, &entries);
                if (ret <= 0)
                        break;
                ret = 0;
                free_entries = _gf_true;

                if (list_empty (&entries.list))
                        goto out;

                if (heal_op == GF_SHD_OP_INDEX_SUMMARY) {
                        ret = glfsh_process_entries (readdir_xl, fd,
                                                     &entries, &offset,
                                                     &num_entries,
                                                     glfsh_print_heal_status);
                        if (ret < 0)
                                goto out;
                } else if (heal_op == GF_SHD_OP_SPLIT_BRAIN_FILES) {
                        ret = glfsh_process_entries (readdir_xl, fd,
                                                     &entries, &offset,
                                                     &num_entries,
                                                     glfsh_print_spb_status);
                        if (ret < 0)
                                goto out;
                } else if (heal_op == GF_SHD_OP_SBRAIN_HEAL_FROM_BRICK) {
                        ret = glfsh_heal_entries (fs, top_subvol, rootloc,
                                                  &entries, &offset,
                                                  &num_entries, xattr_req);
                }
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
                if (heal_op == GF_SHD_OP_INDEX_SUMMARY)
                        printf ("Number of entries: %"PRIu64"\n", num_entries);
                else if (heal_op == GF_SHD_OP_SPLIT_BRAIN_FILES)
                        printf ("Number of entries in split-brain: %"PRIu64"\n"
                                , num_entries);
                else if (heal_op == GF_SHD_OP_SBRAIN_HEAL_FROM_BRICK)
                        printf ("Number of healed entries: %"PRIu64"\n",
                                num_entries);
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
glfsh_print_pending_heals (glfs_t *fs, xlator_t *top_subvol, loc_t *rootloc,
                           xlator_t *xl, gf_xl_afr_op_t heal_op)
{
        int ret = 0;
        loc_t   dirloc = {0};
        fd_t    *fd = NULL;
        int32_t op_errno = 0;
        dict_t *xattr_req = NULL;

        xattr_req = dict_new();
        if (!xattr_req)
                goto out;

        ret = dict_set_int32 (xattr_req, "heal-op", heal_op);
        if (ret)
                goto out;
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

        ret = syncop_dirfd (xl, &dirloc, &fd, GF_CLIENT_PID_GLFS_HEAL);
        if (ret)
                goto out;

        ret = glfsh_crawl_directory (fs, top_subvol, rootloc, xl, fd, &dirloc,
                                     xattr_req);
        if (fd)
                fd_unref (fd);
        if (xattr_req)
                dict_unref (xattr_req);
        if (ret < 0) {
                if (heal_op == GF_SHD_OP_INDEX_SUMMARY)
                        printf ("Failed to find entries with pending"
                                " self-heal\n");
                if (heal_op == GF_SHD_OP_SPLIT_BRAIN_FILES)
                        printf ("Failed to find entries in split-brain\n");
        }
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

static xlator_t*
_brick_path_to_client_xlator (xlator_t *top_subvol, char *hostname,
                              char *brickpath)
{
        int ret             = 0;
        xlator_t *xl        = NULL;
        char *remote_host   = NULL;
        char *remote_subvol = NULL;

        xl = top_subvol;

        while (xl->next)
                xl = xl->next;

        while (xl) {
                if (!strcmp (xl->type, "protocol/client")) {
                        ret = dict_get_str (xl->options, "remote-host",
                                                    &remote_host);
                        if (ret < 0)
                                goto out;
                        ret = dict_get_str (xl->options,
                                            "remote-subvolume", &remote_subvol);
                        if (ret < 0)
                                goto out;
                        if (!strcmp (hostname, remote_host) &&
                            !strcmp (brickpath, remote_subvol))
                                return xl;
                }
                xl = xl->prev;
        }

out:
        return NULL;
}


int
glfsh_gather_heal_info (glfs_t *fs, xlator_t *top_subvol, loc_t *rootloc,
                        gf_xl_afr_op_t heal_op)
{
        xlator_t  *xl       = NULL;
        xlator_t  *afr_xl   = NULL;
        xlator_t  *old_THIS = NULL;

        xl = top_subvol;
        while (xl->next)
                xl = xl->next;
        while (xl) {
                if (strcmp (xl->type, "protocol/client") == 0) {
                        afr_xl = _get_afr_ancestor (xl);
                        if (afr_xl)
                                old_THIS = THIS;
                                THIS = afr_xl;
                                glfsh_print_pending_heals (fs, top_subvol,
                                                           rootloc, xl,
                                                           heal_op);
                                THIS = old_THIS;
                                printf ("\n");
                }

                xl = xl->prev;
        }

        return 0;
}

int
glfsh_heal_splitbrain_file (glfs_t *fs, xlator_t *top_subvol, loc_t *rootloc,
                           char *file, dict_t *xattr_req)
{
        int          ret        = -1;
        int          reval      = 0;
        loc_t        loc        = {0, };
        char        *path       = NULL;
        char        *filename   = NULL;
        struct iatt  iatt       = {0, };
        xlator_t    *xl         = top_subvol;
        dict_t      *xattr_rsp  = NULL;
        char        *sh_fail_msg = NULL;
        int32_t      op_errno   = 0;

        if (!strncmp (file, "gfid:", 5)) {
                filename = gf_strdup(file);
                path = strtok (filename, ":");
                path = strtok (NULL, ";");
                uuid_parse (path, loc.gfid);
                loc.path = gf_strdup (uuid_utoa (loc.gfid));
                loc.inode = inode_new (rootloc->inode->table);
                ret = syncop_lookup (xl, &loc, xattr_req, 0, &xattr_rsp, 0);
                if (ret) {
                        op_errno = -ret;
                        printf ("Lookup failed on %s:%s.\n", file,
                                strerror(op_errno));
                        goto out;
                }
        } else {
                if (file[0] != '/') {
                        printf ("<FILE> must be absolute path w.r.t. the "
                                "volume, starting with '/'\n");
                        ret = -1;
                        goto out;
                }
retry:
                ret = glfs_resolve (fs, xl, file, &loc, &iatt, reval);
                ESTALE_RETRY (ret, errno, reval, &loc, retry);
                if (ret) {
                        printf("Lookup failed on %s:%s\n",
                               file, strerror (errno));
                        goto out;
                }
        }

        ret = syncop_getxattr (xl, &loc, &xattr_rsp, GF_AFR_HEAL_SBRAIN,
                               xattr_req);
        if (ret) {
                op_errno = -ret;
                printf ("Healing %s failed:%s.\n", file, strerror(op_errno));
                goto out;
        }
        ret = dict_get_str (xattr_rsp, "sh-fail-msg", &sh_fail_msg);
        if (!ret) {
                printf ("Healing %s failed: %s.\n", file, sh_fail_msg);
                ret = -1;
                goto out;
        }
        printf ("Healed %s.\n", file);
        ret = 0;
out:
        if (xattr_rsp)
                dict_unref (xattr_rsp);
        return ret;
}

int
glfsh_heal_from_brick (glfs_t *fs, xlator_t *top_subvol, loc_t *rootloc,
                      char *hostname, char *brickpath, char *file)
{
        int       ret       = -1;
        dict_t   *xattr_req = NULL;
        xlator_t *client    = NULL;
        fd_t     *fd        = NULL;
        loc_t     dirloc    = {0};
        int32_t   op_errno  = 0;

        xattr_req = dict_new();
        if (!xattr_req)
                goto out;
        ret = dict_set_int32 (xattr_req, "heal-op",
                              GF_SHD_OP_SBRAIN_HEAL_FROM_BRICK);
        if (ret)
                goto out;
        client = _brick_path_to_client_xlator (top_subvol, hostname, brickpath);
        if (!client) {
                printf("\"%s:%s\"- No such brick available in the volume.\n",
                       hostname, brickpath);
                ret = -1;
                goto out;
        }
        ret = dict_set_str (xattr_req, "child-name", client->name);
        if (ret)
                goto out;
        if (file)
                ret = glfsh_heal_splitbrain_file (fs, top_subvol, rootloc, file,
                                                 xattr_req);
        else {
                ret = glfsh_get_index_dir_loc (rootloc, client, &dirloc,
                                               &op_errno);
                ret = syncop_dirfd (client, &dirloc, &fd,
                                    GF_CLIENT_PID_GLFS_HEAL);
                if (ret)
                        goto out;
                ret = glfsh_crawl_directory (fs, top_subvol, rootloc, client,
                                             fd, &dirloc, xattr_req);
                if (fd)
                        fd_unref (fd);
        }
out:
        if (xattr_req)
                dict_unref (xattr_req);
        loc_wipe (&dirloc);
        return ret;
}

int
glfsh_heal_from_bigger_file (glfs_t *fs, xlator_t *top_subvol, loc_t *rootloc,
                            char *file)
{

        int ret = -1;
        dict_t *xattr_req = NULL;

        xattr_req = dict_new();
        if (!xattr_req)
                goto out;
        ret = dict_set_int32 (xattr_req, "heal-op",
                              GF_SHD_OP_SBRAIN_HEAL_FROM_BIGGER_FILE);
        if (ret)
                goto out;
        ret = glfsh_heal_splitbrain_file (fs, top_subvol, rootloc, file,
                                         xattr_req);
out:
        if (xattr_req)
                dict_unref (xattr_req);
        return ret;
}

int
main (int argc, char **argv)
{
        glfs_t    *fs = NULL;
        int        ret = 0;
        char      *volname = NULL;
        xlator_t  *top_subvol = NULL;
        loc_t     rootloc = {0};
        char      logfilepath[PATH_MAX] = {0};
        char      *hostname = NULL;
        char      *path = NULL;
        char      *file = NULL;
        gf_xl_afr_op_t heal_op = -1;

        if (argc < 2) {
                printf (USAGE_STR, argv[0]);
                ret = -1;
                goto out;
        }
        volname = argv[1];
        switch (argc) {
        case 2:
                heal_op = GF_SHD_OP_INDEX_SUMMARY;
                break;
        case 3:
                if (!strcmp (argv[2], "split-brain-info")) {
                        heal_op = GF_SHD_OP_SPLIT_BRAIN_FILES;
                } else {
                        printf (USAGE_STR, argv[0]);
                        ret = -1;
                        goto out;
                }
                break;
        case 4:
                if (!strcmp (argv[2], "bigger-file")) {
                        heal_op = GF_SHD_OP_SBRAIN_HEAL_FROM_BIGGER_FILE;
                        file = argv[3];
                } else if (!strcmp (argv[2], "source-brick")) {
                        heal_op = GF_SHD_OP_SBRAIN_HEAL_FROM_BRICK;
                        hostname = strtok (argv[3], ":");
                        path = strtok (NULL, ":");
                } else {
                        printf (USAGE_STR, argv[0]);
                        ret = -1;
                        goto out;
                }
                break;
        case 5:
                if (!strcmp (argv[2], "source-brick")) {
                        heal_op = GF_SHD_OP_SBRAIN_HEAL_FROM_BRICK;
                        hostname = strtok (argv[3], ":");
                        path = strtok (NULL, ":");
                        file = argv[4];
                } else {
                        printf (USAGE_STR, argv[0]);
                        ret = -1;
                        goto out;
                }
                break;
        default:
                printf (USAGE_STR, argv[0]);
                ret = -1;
                goto out;
        }

        fs = glfs_new (volname);
        if (!fs) {
                ret = -1;
                printf ("Not able to initialize volume '%s'\n", volname);
                goto out;
        }

        ret = glfs_set_volfile_server (fs, "tcp", "localhost", 24007);
        if (ret) {
                printf("Setting the volfile server failed, %s\n", strerror (errno));
                goto out;
        }
        snprintf (logfilepath, sizeof (logfilepath),
                  DEFAULT_HEAL_LOG_FILE_DIRECTORY"/glfsheal-%s.log", volname);
        ret = glfs_set_logging(fs, logfilepath, GF_LOG_INFO);
        if (ret < 0) {
                printf ("Failed to set the log file path, %s\n", strerror (errno));
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

        switch (heal_op) {
        case GF_SHD_OP_INDEX_SUMMARY:
        case GF_SHD_OP_SPLIT_BRAIN_FILES:
                ret = glfsh_gather_heal_info (fs, top_subvol, &rootloc,
                                              heal_op);
                break;
        case GF_SHD_OP_SBRAIN_HEAL_FROM_BIGGER_FILE:
                ret = glfsh_heal_from_bigger_file (fs, top_subvol,
                                                   &rootloc, file);
                        break;
        case GF_SHD_OP_SBRAIN_HEAL_FROM_BRICK:
                ret = glfsh_heal_from_brick (fs, top_subvol, &rootloc,
                                             hostname, path, file);
                break;
        default:
                ret = -1;
                break;
        }

        loc_wipe (&rootloc);
        glfs_subvol_done (fs, top_subvol);
        glfs_fini (fs);

        return ret;
out:
        if (fs && top_subvol)
                glfs_subvol_done (fs, top_subvol);
        loc_wipe (&rootloc);
        if (fs)
                glfs_fini (fs);

        return ret;
}
