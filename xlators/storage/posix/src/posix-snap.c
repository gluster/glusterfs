/*
   Copyright (c) 2018 Red Hat, Inc. <http://www.redhat.com>
   This file is part of GlusterFS.

   This file is licensed to you under your choice of the GNU Lesser
   General Public License, version 3 or any later version (LGPLv3 or
   later), or the GNU General Public License, version 2 (GPLv2), in all
   cases as published by the Free Software Foundation.
*/

#include "posix-snap.h"

/*
 * =========================================================
 * INFRA CODE REQUIRED FOR FILE SNAPSHOTS
 * =========================================================
 */

/**
 * @basename: Name of the snapshot (atleast from gluster's perspective)
 * @gfid: gfid of the file whose snapshot is being referred to here
 *
 * If @basename is NULL, then only the gfid container that has all the
 * snapshots is considered.
 *
 * Ex: if the gfid is "00000000-0000-0000-0000-000000000000", then this
 * function will return the length of the path
 * <brick export>/.glusterfs/fsnaps/00/00/00000000-0000-0000-0000-000000000000"
 * if @basename is NULL. If not, then along with the above path, the length
 * of the basename is also calculated and total length is returned

 **/
int
posix_handle_snap_path (xlator_t *this, uuid_t gfid, const char *basename,
                        char *buf, size_t buflen)
{
        struct posix_private *priv = NULL;
        char                 *uuid_str = NULL;
        int                   len = 0;

        priv = this->private;

        len = SNAP_CONT_ABSPATH_LEN (this);

        if (basename) {
                len += (strlen (basename) + 1);
        } else {
                len += 256;  /* worst-case NAME_MAX */
        }

        if ((buflen < len) || !buf)
                return len;

        uuid_str = uuid_utoa (gfid);

        if (basename) {
                len = snprintf (buf, buflen, "%s/%s/%02x/%02x/%s/%s", priv->base_path,
                                GF_SNAPS_PATH, gfid[0], gfid[1], uuid_str, basename);
        } else {
                len = snprintf (buf, buflen, "%s/%s/%02x/%02x/%s", priv->base_path,
                                GF_SNAPS_PATH, gfid[0], gfid[1], uuid_str);
        }

        return len;
}

/**
 * This function takes @gfid as the input argument which has
 * been removed. Here @gfid removed means, all the references
 * to that gfid (i.e. hardlinks that are created in gluster volume
 * namespace) have been removed. So this function, receiving a gfid
 * means, the actual gfid has been removed, and now this function
 * has to ensure removing of all the snapshots of that gfid (or file).
 * Removing all the file snapshots synchronously with the removal
 * of gfid might affect the application (because the gfid removal
 * would happen as part of application's unlink operation OR as part
 * of the close () operation issued by the application on last fd
 * for that gfid). So, instead of making application wait, this
 * function moves the snapshots to a trash location to be picked
 * up and cleaned by the janitor thread.
 */
int
posix_remove_file_snapshots (xlator_t *this, uuid_t gfid)
{
        int ret = -1;
        struct posix_private *priv = NULL;
        char  *snap_loc= NULL;
        char  new_loc[PATH_MAX] = {0, };

        priv = this->private;

        SNAP_MAKE_CONT_ABSPATH (snap_loc, this, gfid);

        snprintf (new_loc, PATH_MAX, "%s/%s", priv->snap_trash_path,
                  uuid_utoa (gfid));

        ret = sys_rename (snap_loc, new_loc);
        if (ret) {
                gf_msg (this->name, GF_LOG_WARNING, errno,
                        P_MSG_RENAME_FAILED,  "failed to "
                        "move the snapshots of gfid %s to trash (%s)" ,
                        uuid_utoa (gfid), strerror (errno));
                goto out;
        }

        pthread_cond_signal (&priv->janitor_cond);

out:
        return ret;
}


/*
 * ===============================================================
 * SNAPSHOT OPERATIONS
 * ===============================================================
 */

/**
 * This function constructs and creates the location where the
 * the snapshot is created and placed, 
 * @path: path or gfid handle path of the file
 * @snap: location identifier for where snapshot is created
 * @snap_name: Name of the snapshot
 **/
int
posix_take_snap (xlator_t *this, const char *path, char *snap,
                 char *snap_name)
{
        int ret = -1;
        int fd_src = -1;
        int fd_dst = -1;
        char snap_loc[PATH_MAX] = {0, };

        GF_VALIDATE_OR_GOTO ("posix", this, out);
        GF_VALIDATE_OR_GOTO (this->name, path, out);
        GF_VALIDATE_OR_GOTO (this->name, snap, out);

        snprintf (snap_loc, PATH_MAX, "%s/%s", snap, snap_name);

        ret = sys_mkdir (snap, 0755);
        if (ret) {
                gf_msg (this->name, GF_LOG_ERROR, errno,
                        P_MSG_MKDIR_FAILED,
                        "failed to create the actual directory "
                        "containing snapshot %s (%s)",
                        snap, strerror (errno));
                goto out;
        }

        fd_src = open (path, O_RDONLY);
        if (fd_src < 0) {
                gf_msg (this->name, GF_LOG_ERROR, errno,
                        P_MSG_OPEN_FAILED, "failed to open "
                        "the file %s for reading (%s)", path,
                        strerror (errno));
                goto out;
        }

        fd_dst = open (snap_loc, O_CREAT|O_WRONLY|O_EXCL);
        if (fd_dst < 0) {
                gf_msg (this->name, GF_LOG_ERROR, errno,
                        P_MSG_OPEN_FAILED, "failed to create "
                        "the snap file %s (%s)", snap,
                        strerror (errno));
                goto out;
        }

        /*
         * As per "strace cp --reflink=always <src> <dst>"
         * the following syscall is used to create reflink.
         * ioctl (fd_dst, BTRFS_IOC_CLONE or FICLONE, fd_src);
         * BTRFS_IOC_CLONE is for doing reflink in btrfs.
         * TODO: Also make it work with BTRFS_IOC_CLONE
         */

        ret = ioctl (fd_dst, FICLONE, fd_src);
        if (ret < 0)
                gf_msg (this->name, GF_LOG_ERROR, errno,
                        P_MSG_REFLINK_FAILED, "failed to "
                        "create the reflink %s for the file %s "
                        "(%s)", snap, path, strerror (errno));

out:
        if (fd_src > 0)
                close (fd_src);
        if (fd_dst > 0)
                close (fd_dst);
        return ret;
}

/**
 * The snapshot name is assumed in this way by this function
 * @name: is the name of the snapshot provided by consumer of
 *        this function. So usually it means, @name is the
 *        value of the xattr used to send snap create op
 *        via setxattr.
 *        If @name is NULL, then a snap name is generated by
 *        this function which is "<gfid>-<time>"
 * @path: path or the gfid handle of the file
 * @gfid: gfid of the file whose snapshot is being taken
 **/
int
posix_snap (xlator_t *this, const char *path, uuid_t gfid,
            char *name)
{
        char        *newpath = NULL;
        struct stat  newbuf;
        int          ret = -1;
        char         snap_name[NAME_MAX] = {0, };
        char         new_snap[PATH_MAX] = {0, };


        SNAP_MAKE_CONT_ABSPATH (newpath, this, gfid);

        if (!name)
                SNAP_MAKE_NAME (snap_name, gfid);
        else
                snprintf (snap_name, NAME_MAX, "%s", name);

        snprintf (new_snap, PATH_MAX, "%s/%s",  newpath, snap_name);

        ret = sys_lstat (newpath, &newbuf);
        if (ret == -1 && errno != ENOENT) {
                gf_msg (this->name, GF_LOG_WARNING, errno,
                        P_MSG_HANDLE_CREATE,
                        "%s", newpath);
                return -1;
        }

        if (ret == -1 && errno == ENOENT) {
                ret = posix_handle_mkdir_hashes (this, newpath);
                if (ret) {
                        gf_msg (this->name, GF_LOG_WARNING, errno,
                                P_MSG_HANDLE_CREATE, "mkdir %s failed ",
                                newpath);
                        return -1;
                }

                ret = sys_mkdir (newpath, 0700);

                if (ret) {
                        gf_msg (this->name, GF_LOG_WARNING, errno,
                                P_MSG_SNAP_FILE_SNAP_CONT_FAILED,
                                "creation of snapshot container %s"
                                "failed ", newpath);
                        return -1;
                }

                ret = sys_lstat (newpath, &newbuf);
                if (ret) {
                        gf_msg (this->name, GF_LOG_WARNING, errno,
                                P_MSG_SNAP_FILE_SNAP_CONT_FAILED,
                                "lstat on %s failed", newpath);
                        return -1;
                }
        }

        ret = posix_take_snap (this, path, new_snap, snap_name);
        if (ret < 0)
                gf_msg (this->name, GF_LOG_ERROR, 0,
                        P_MSG_FILE_SNAP_FAILED, "failed to take "
                        "the snapshot of the file %s (gfid: %s) ",
                        path, uuid_utoa (gfid));

        return ret;
}

/**
 *
 * This function is the API consumed by posix to take the snapshot.
 * It gets the name of the snapshot from the dict @xattr
 * and then calls approprioate function, which constructs the location
 * of the snapshot and then takes further actions to delete it.
 * @loc: location identifier for the file whose snapshot has to be taken
 * @fd: file descriptor of the file whose snapshot has to be taken
 * @xattr: dictionary which contains the name of the snapshot
 * @xdata: dictionary for customiztion of operation and extra info
 *
 * for path based setxattr or operation, fd is NULL and for fd based snap,
 * (fsetxattr as of now), loc is NULL.
 *
 * xattr contains the value for the setxattr key via shich the snapshot
 * create was sent. And xdata is for any other information that can be
 * sent for customization of this operation.
 *
 **/
int32_t
posix_file_snap_create (xlator_t *this, loc_t *loc, fd_t *fd, dict_t *xattr,
                        dict_t *xdata)
{
        int32_t   ret = -1;
        char     *file_path = NULL;
        char     *name = NULL;
        void     *data  = NULL;

        GF_VALIDATE_OR_GOTO ("posix", this, out);

        ret = dict_get_ptr (xattr, GF_XATTR_FILE_SNAP, &data);
        if (ret < 0) {
                gf_msg (this->name, GF_LOG_ERROR, 0,
                        P_MSG_DICT_GET_FAILED, "failed to get"
                        "the snapshot name for the gfid %s ",
                        uuid_utoa (loc?loc->gfid:fd->inode->gfid));
                goto out;
        }

        name = (char *)data;

        if (loc) {
                if (LOC_HAS_ABSPATH (loc))
                        MAKE_REAL_PATH (file_path, this, loc->path);
                else
                        MAKE_HANDLE_PATH (file_path, this, loc->gfid,
                                          NULL);

                //ret = posix_snap (this, file_path, loc->gfid, NULL);
                ret = posix_snap (this, file_path, loc->gfid, name);
        }

        if (fd) {
                MAKE_HANDLE_PATH (file_path, this, fd->inode->gfid, NULL);
                //ret = posix_snap (this, file_path, fd->inode->gfid, NULL);
                ret = posix_snap (this, file_path, fd->inode->gfid, name);
        }

out:
        return ret;
}

/**
 * This function constructs the trash location to which the
 * the snapshot has to be moved to, for janitor thread to
 * pick up and clean.
 * @gfid: gfid of the file whose snapshot is being removed
 * @snap: location of the snapshot
 * @name: Name of the snapshot
 **/
int
posix_delete_snap (xlator_t *this, uuid_t gfid, char *snap,
                   char *name)
{
        int32_t  ret = -1;
        struct posix_private *priv = NULL;
        char   new_loc[PATH_MAX] = {0, };

        GF_VALIDATE_OR_GOTO ("posix", this, out);

        priv = this->private;

        snprintf (new_loc, PATH_MAX, "%s/%s", priv->snap_trash_path,
                  name);

        ret = sys_rename (snap, new_loc);
        if (ret) {
                gf_msg (this->name, GF_LOG_WARNING, errno,
                        P_MSG_RENAME_FAILED,  "failed to "
                        "move the snapshot %s of gfid %s to trash (%s)",
                        snap, uuid_utoa (gfid), strerror (errno));
                goto out;
        }

        pthread_cond_signal (&priv->janitor_cond);

out:
        return ret;
}

/**
 * This function constructs the location of the snapshot based
 * on the gfid of the file and snapshot name. It then calls
 * the function posix_delete_snap to actual remove the snapshot
 * @path: path of the file or gfid handle location of the file
 * @gfid: gfid of the file whose snapshot is being removed
 * @name: name of the snapshot to be removed
 **/
int
posix_snap_remove (xlator_t *this, const char *path, uuid_t gfid,
                   char *name)
{
        char        *newpath = NULL;
        struct stat  newbuf;
        int          ret = -1;
        char         snap[PATH_MAX] = {0, };


        SNAP_MAKE_CONT_ABSPATH (newpath, this, gfid);

        if (!name) {
                gf_msg (this->name, GF_LOG_ERROR, 0,
                        P_MSG_SNAP_NAME_NULL, "name of the "
                        "snapshot to be removed for the gfid %s "
                        "is NULL", uuid_utoa (gfid));
                ret = -1;
                goto out;
        }

        snprintf (snap, PATH_MAX, "%s/%s",  newpath, name);

        ret = sys_lstat (newpath, &newbuf);
        if (ret == -1)
                gf_msg (this->name, GF_LOG_WARNING, errno,
                        P_MSG_HANDLE_CREATE, "failed to find the "
                        "path %s of the snapshot for gfid %s"
                        "%s", newpath, uuid_utoa (gfid),
                        strerror (errno));


        ret = posix_delete_snap (this, gfid, snap, name);
        if (ret < 0)
                gf_msg (this->name, GF_LOG_ERROR, 0,
                        P_MSG_FILE_SNAP_FAILED, "failed to take "
                        "the snapshot of the file %s (gfid: %s) ",
                        path, uuid_utoa (gfid));

out:
        return ret;
}

/**
 * This function is the API consumed by posix to remove the snapshot.
 * It get the name of the snapshot to be removed from the dict @xattr
 * and then calls approprioate function, which constructs the location
 * of the snapshot and then takes further actions to delete it.
 * @loc: location identifier for the file whose snapshot has to be removed
 * @fd: file descriptor of the file whose snapshot has to be removed
 * @xattr: dictionary which contains the name of the snapshot to be removed
 * @xdata: dictionary for customiztion of operation and extra info
 **/
int32_t
posix_file_snap_remove (xlator_t *this, loc_t *loc, fd_t *fd, dict_t *xattr,
                        dict_t *xdata)
{
        int32_t  ret       = -1;
        char    *name      = NULL;
        char    *file_path = NULL;
        void    *data      = NULL;

        GF_VALIDATE_OR_GOTO ("posix", this, out);

        ret = dict_get_ptr (xattr, GF_XATTR_FILE_SNAP_REMOVE,
                            &data);
        if (ret < 0) {
                gf_msg (this->name, GF_LOG_ERROR, 0,
                        P_MSG_DICT_GET_FAILED, "failed to get"
                        "the snapshot name to be deleted for the "
                        "gfid %s", uuid_utoa (loc?loc->gfid:fd->inode->gfid));
                goto out;
        }

        name = (char *)data;

        if (loc) {
                if (LOC_HAS_ABSPATH (loc))
                        MAKE_REAL_PATH (file_path, this, loc->path);
                else
                        MAKE_HANDLE_PATH (file_path, this, loc->gfid,
                                          NULL);

                /* TODO: Add code to get the name of the snapshot from
                 *       @dict and send it as argument to posix_snap
                 *       instead of NULL.
                 */
                ret = posix_snap_remove (this, file_path, loc->gfid, name);
        }

        if (fd) {
                MAKE_HANDLE_PATH (file_path, this, fd->inode->gfid, NULL);
                ret = posix_snap_remove (this, file_path, fd->inode->gfid, name);
        }

out:
        return ret;
}
