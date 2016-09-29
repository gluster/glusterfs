/*
 *  Copyright (c) 2013 Red Hat, Inc. <http://www.redhat.com>
 *  This file is part of GlusterFS.
 *
 *  This file is licensed to you under your choice of the GNU Lesser
 *  General Public License, version 3 or any later version (LGPLv3 or
 *  later), or the GNU General Public License, version 2 (GPLv2), in all
 *  cases as published by the Free Software Foundation.
 */


#include "glfs-internal.h"
#include "glfs-mem-types.h"
#include "syncop.h"
#include "glfs.h"
#include "glfs-handles.h"
#include "gfapi-messages.h"

int
glfs_listxattr_process (void *value, size_t size, dict_t *xattr);

static void
glfs_iatt_from_stat (struct stat *stat, int valid, struct iatt *iatt,
                     int *glvalid)
{
        /* validate in args */
        if ((stat == NULL) || (iatt == NULL) || (glvalid == NULL)) {
                errno = EINVAL;
                return;
        }

        *glvalid = 0;

        if (valid & GFAPI_SET_ATTR_MODE) {
                iatt->ia_prot = ia_prot_from_st_mode (stat->st_mode);
                *glvalid |= GF_SET_ATTR_MODE;
        }

        if (valid & GFAPI_SET_ATTR_UID) {
                iatt->ia_uid = stat->st_uid;
                *glvalid |= GF_SET_ATTR_UID;
        }

        if (valid & GFAPI_SET_ATTR_GID) {
                iatt->ia_gid = stat->st_gid;
                *glvalid |= GF_SET_ATTR_GID;
        }

        if (valid & GFAPI_SET_ATTR_ATIME) {
                iatt->ia_atime = stat->st_atime;
                iatt->ia_atime_nsec = ST_ATIM_NSEC (stat);
                *glvalid |= GF_SET_ATTR_ATIME;
        }

        if (valid & GFAPI_SET_ATTR_MTIME) {
                iatt->ia_mtime = stat->st_mtime;
                iatt->ia_mtime_nsec = ST_MTIM_NSEC (stat);
                *glvalid |= GF_SET_ATTR_MTIME;
        }

        return;
}

struct glfs_object *
pub_glfs_h_lookupat (struct glfs *fs, struct glfs_object *parent,
                     const char *path, struct stat *stat, int follow)
{
        int                      ret = 0;
        xlator_t                *subvol = NULL;
        inode_t                 *inode = NULL;
        struct iatt              iatt = {0, };
        struct glfs_object      *object = NULL;
        loc_t                    loc = {0, };

        DECLARE_OLD_THIS;

        /* validate in args */
        if (path == NULL) {
                errno = EINVAL;
                return NULL;
        }

        __GLFS_ENTRY_VALIDATE_FS (fs, invalid_fs);

        /* get the active volume */
        subvol = glfs_active_subvol (fs);
        if (!subvol) {
                errno = EIO;
                goto out;
        }

        /* get/refresh the in arg objects inode in correlation to the xlator */
        if (parent) {
                inode = glfs_resolve_inode (fs, subvol, parent);
                if (!inode) {
                        errno = ESTALE;
                        goto out;
                }
        }

        /* fop/op */
        ret = glfs_resolve_at (fs, subvol, inode, path, &loc, &iatt,
                                    follow, 0);

        /* populate out args */
        if (!ret) {
                if (stat)
                        glfs_iatt_to_stat (fs, &iatt, stat);

                ret = glfs_create_object (&loc, &object);
        }

out:
        loc_wipe (&loc);

        if (inode)
                inode_unref (inode);

        glfs_subvol_done (fs, subvol);

        __GLFS_EXIT_FS;

invalid_fs:
        return object;
}

GFAPI_SYMVER_PUBLIC_DEFAULT(glfs_h_lookupat, 3.7.4);

struct glfs_object *
pub_glfs_h_lookupat34 (struct glfs *fs, struct glfs_object *parent,
                       const char *path, struct stat *stat)
{
        return pub_glfs_h_lookupat (fs, parent, path, stat, 0);
}

GFAPI_SYMVER_PUBLIC(glfs_h_lookupat34, glfs_h_lookupat, 3.4.2);

int
pub_glfs_h_statfs (struct glfs *fs, struct glfs_object *object,
                   struct statvfs *statvfs)
{
        int              ret = -1;
        xlator_t        *subvol = NULL;
        inode_t         *inode = NULL;
        loc_t            loc = {0, };

        DECLARE_OLD_THIS;

        /* validate in args */
        if ((fs == NULL) || (object == NULL || statvfs == NULL)) {
                errno = EINVAL;
                return -1;
        }

        __GLFS_ENTRY_VALIDATE_FS (fs, invalid_fs);

        /* get the active volume */
        subvol = glfs_active_subvol (fs);
        if (!subvol) {
                ret = -1;
                errno = EIO;
                goto out;
        }

        /* get/refresh the in arg objects inode in correlation to the xlator */
        inode = glfs_resolve_inode (fs, subvol, object);
        if (!inode) {
                errno = ESTALE;
                goto out;
        }

        /* populate loc */
        GLFS_LOC_FILL_INODE (inode, loc, out);

        /* fop/op */
        ret = syncop_statfs (subvol, &loc, statvfs, NULL, NULL);
        DECODE_SYNCOP_ERR (ret);

        loc_wipe (&loc);

out:
        if (inode)
                inode_unref (inode);

        glfs_subvol_done (fs, subvol);

        __GLFS_EXIT_FS;

invalid_fs:
        return ret;
}

GFAPI_SYMVER_PUBLIC_DEFAULT(glfs_h_statfs, 3.7.0);

int
pub_glfs_h_stat (struct glfs *fs, struct glfs_object *object, struct stat *stat)
{
        int              ret = -1;
        xlator_t        *subvol = NULL;
        inode_t         *inode = NULL;
        loc_t            loc = {0, };
        struct iatt      iatt = {0, };

        DECLARE_OLD_THIS;

        /* validate in args */
        if ((fs == NULL) || (object == NULL)) {
                errno = EINVAL;
                return -1;
        }

        __GLFS_ENTRY_VALIDATE_FS (fs, invalid_fs);

        /* get the active volume */
        subvol = glfs_active_subvol (fs);
        if (!subvol) {
                ret = -1;
                errno = EIO;
                goto out;
        }

        /* get/refresh the in arg objects inode in correlation to the xlator */
        inode = glfs_resolve_inode (fs, subvol, object);
        if (!inode) {
                errno = ESTALE;
                goto out;
        }

        /* populate loc */
        GLFS_LOC_FILL_INODE (inode, loc, out);

        /* fop/op */
        ret = syncop_stat (subvol, &loc, &iatt, NULL, NULL);
        DECODE_SYNCOP_ERR (ret);

        /* populate out args */
        if (!ret && stat) {
                glfs_iatt_to_stat (fs, &iatt, stat);
        }
out:
        loc_wipe (&loc);

        if (inode)
                inode_unref (inode);

        glfs_subvol_done (fs, subvol);

        __GLFS_EXIT_FS;

invalid_fs:
        return ret;
}

GFAPI_SYMVER_PUBLIC_DEFAULT(glfs_h_stat, 3.4.2);


int
pub_glfs_h_getattrs (struct glfs *fs, struct glfs_object *object,
                     struct stat *stat)
{
        int                      ret = -1;
        xlator_t                *subvol = NULL;
        inode_t                 *inode = NULL;
        struct iatt              iatt = {0, };

        /* validate in args */
        if ((fs == NULL) || (object == NULL)) {
                errno = EINVAL;
                return -1;
        }

        DECLARE_OLD_THIS;
        __GLFS_ENTRY_VALIDATE_FS (fs, invalid_fs);

        /* get the active volume */
        subvol = glfs_active_subvol (fs);
        if (!subvol) {
                ret = -1;
                errno = EIO;
                goto out;
        }

        /* get/refresh the in arg objects inode in correlation to the xlator */
        inode = glfs_resolve_inode (fs, subvol, object);
        if (!inode) {
                ret = 0;
                errno = ESTALE;
                goto out;
        }

        /* fop/op */
        ret = glfs_resolve_base (fs, subvol, inode, &iatt);

        /* populate out args */
        if (!ret && stat) {
                glfs_iatt_to_stat (fs, &iatt, stat);
        }

out:
        if (inode)
                inode_unref (inode);

        glfs_subvol_done (fs, subvol);

        __GLFS_EXIT_FS;

invalid_fs:
        return ret;
}

GFAPI_SYMVER_PUBLIC_DEFAULT(glfs_h_getattrs, 3.4.2);


int
glfs_h_getxattrs_common (struct glfs *fs, struct glfs_object *object,
                         dict_t **xattr, const char *name,
                         gf_boolean_t is_listxattr)
{
        int                 ret = 0;
        xlator_t        *subvol = NULL;
        inode_t         *inode = NULL;
        loc_t            loc = {0, };

        /* validate in args */
        if ((fs == NULL) || (object == NULL)) {
                errno = EINVAL;
                return -1;
        }

        if (!is_listxattr) {
                if (!name || *name == '\0') {
                        errno = EINVAL;
                        return -1;
                }

                if (strlen(name) > GF_XATTR_NAME_MAX) {
                        errno = ENAMETOOLONG;
                        return -1;
                }
        }
        /* get the active volume */
        subvol = glfs_active_subvol (fs);
        if (!subvol) {
                ret = -1;
                errno = EIO;
                goto out;
        }

        /* get/refresh the in arg objects inode in correlation to the xlator */
        inode = glfs_resolve_inode (fs, subvol, object);
        if (!inode) {
                errno = ESTALE;
                goto out;
        }

        /* populate loc */
        GLFS_LOC_FILL_INODE (inode, loc, out);

        ret = syncop_getxattr (subvol, &loc, xattr, name, NULL, NULL);
        DECODE_SYNCOP_ERR (ret);

out:
        loc_wipe (&loc);

        if (inode)
                inode_unref (inode);

        glfs_subvol_done (fs, subvol);

        return ret;
}


int
pub_glfs_h_getxattrs (struct glfs *fs, struct glfs_object *object,
                      const char *name, void *value, size_t size)
{
        int                    ret   = -1;
        dict_t                *xattr = NULL;

        /* validate in args */
        if ((fs == NULL) || (object == NULL)) {
                errno = EINVAL;
                return -1;
        }

        DECLARE_OLD_THIS;
        __GLFS_ENTRY_VALIDATE_FS (fs, invalid_fs);

        ret = glfs_h_getxattrs_common (fs, object, &xattr, name,
                                       (name == NULL));
        if (ret)
                goto out;

        /* If @name is NULL, means get all the xattrs (i.e listxattr). */
        if (name)
                ret = glfs_getxattr_process (value, size, xattr, name);
        else
                ret = glfs_listxattr_process (value, size, xattr);

out:
        if (xattr)
                dict_unref (xattr);

        __GLFS_EXIT_FS;

invalid_fs:
        return ret;
}

GFAPI_SYMVER_PUBLIC_DEFAULT(glfs_h_getxattrs, 3.5.1);

int
pub_glfs_h_setattrs (struct glfs *fs, struct glfs_object *object,
                     struct stat *stat, int valid)
{
        int              ret = -1;
        xlator_t        *subvol = NULL;
        inode_t         *inode = NULL;
        loc_t            loc = {0, };
        struct iatt      iatt = {0, };
        int              glvalid = 0;

        /* validate in args */
        if ((fs == NULL) || (object == NULL) || (stat == NULL)) {
                errno = EINVAL;
                return -1;
        }

        DECLARE_OLD_THIS;
        __GLFS_ENTRY_VALIDATE_FS (fs, invalid_fs);

        /* get the active volume */
        subvol = glfs_active_subvol (fs);
        if (!subvol) {
                ret = -1;
                errno = EIO;
                goto out;
        }

        /* get/refresh the in arg objects inode in correlation to the xlator */
        inode = glfs_resolve_inode (fs, subvol, object);
        if (!inode) {
                errno = ESTALE;
                goto out;
        }

        /* map valid masks from in args */
        glfs_iatt_from_stat (stat, valid, &iatt, &glvalid);

        /* populate loc */
        GLFS_LOC_FILL_INODE (inode, loc, out);

        /* fop/op */
        ret = syncop_setattr (subvol, &loc, &iatt, glvalid, 0, 0, NULL, NULL);
        DECODE_SYNCOP_ERR (ret);
out:
        loc_wipe (&loc);

        if (inode)
                inode_unref (inode);

        glfs_subvol_done (fs, subvol);

        __GLFS_EXIT_FS;

invalid_fs:
        return ret;
}

GFAPI_SYMVER_PUBLIC_DEFAULT(glfs_h_setattrs, 3.4.2);


int
pub_glfs_h_setxattrs (struct glfs *fs, struct glfs_object *object,
                      const char *name, const void *value, size_t size,
                      int flags)
{
        int              ret = -1;
        xlator_t        *subvol = NULL;
        inode_t         *inode = NULL;
        loc_t            loc = {0, };
        dict_t          *xattr = NULL;

        /* validate in args */
        if ((fs == NULL) || (object == NULL) ||
                 (name == NULL) || (value == NULL)) {
                errno = EINVAL;
                return -1;
        }

        if (!name || *name == '\0') {
                errno = EINVAL;
                return -1;
        }

        if (strlen(name) > GF_XATTR_NAME_MAX) {
                errno = ENAMETOOLONG;
                return -1;
        }

        DECLARE_OLD_THIS;
        __GLFS_ENTRY_VALIDATE_FS (fs, invalid_fs);

        /* get the active volume */
        subvol = glfs_active_subvol (fs);
        if (!subvol) {
                ret = -1;
                errno = EIO;
                goto out;
        }

        /* get/refresh the in arg objects inode in correlation to the xlator */
        inode = glfs_resolve_inode (fs, subvol, object);
        if (!inode) {
                errno = ESTALE;
                goto out;
        }

        xattr = dict_for_key_value (name, value, size);
        if (!xattr) {
                ret = -1;
                errno = ENOMEM;
                goto out;
        }

        /* populate loc */
        GLFS_LOC_FILL_INODE (inode, loc, out);

        /* fop/op */
        ret = syncop_setxattr (subvol, &loc, xattr, flags, NULL, NULL);
        DECODE_SYNCOP_ERR (ret);

out:
        loc_wipe (&loc);

        if (inode)
                inode_unref (inode);

        if (xattr)
                dict_unref (xattr);

        glfs_subvol_done (fs, subvol);

        __GLFS_EXIT_FS;

invalid_fs:
        return ret;
}

GFAPI_SYMVER_PUBLIC_DEFAULT(glfs_h_setxattrs, 3.5.0);


int
pub_glfs_h_removexattrs (struct glfs *fs, struct glfs_object *object,
                         const char *name)
{
        int              ret = -1;
        xlator_t        *subvol = NULL;
        inode_t         *inode = NULL;
        loc_t            loc = {0, };

        /* validate in args */
        if ((fs == NULL) || (object == NULL) || (name == NULL)) {
                errno = EINVAL;
                return -1;
        }

        DECLARE_OLD_THIS;
        __GLFS_ENTRY_VALIDATE_FS (fs, invalid_fs);

        /* get the active volume */
        subvol = glfs_active_subvol (fs);
        if (!subvol) {
                ret = -1;
                errno = EIO;
                goto out;
        }

        /* get/refresh the in arg objects inode in correlation to the xlator */
        inode = glfs_resolve_inode (fs, subvol, object);
        if (!inode) {
                errno = ESTALE;
                goto out;
        }

        /* populate loc */
        GLFS_LOC_FILL_INODE (inode, loc, out);

        /* fop/op */
        ret = syncop_removexattr (subvol, &loc, name, NULL, NULL);
        DECODE_SYNCOP_ERR (ret);

out:
        loc_wipe (&loc);

        if (inode)
                inode_unref (inode);

        glfs_subvol_done (fs, subvol);

        __GLFS_EXIT_FS;

invalid_fs:
        return ret;
}

GFAPI_SYMVER_PUBLIC_DEFAULT(glfs_h_removexattrs, 3.5.1);


struct glfs_fd *
pub_glfs_h_open (struct glfs *fs, struct glfs_object *object, int flags)
{
        int              ret = -1;
        struct glfs_fd  *glfd = NULL;
        xlator_t        *subvol = NULL;
        inode_t         *inode = NULL;
        loc_t            loc = {0, };

        /* validate in args */
        if ((fs == NULL) || (object == NULL)) {
                errno = EINVAL;
                return NULL;
        }

        DECLARE_OLD_THIS;
        __GLFS_ENTRY_VALIDATE_FS (fs, invalid_fs);

        /* get the active volume */
        subvol = glfs_active_subvol (fs);
        if (!subvol) {
                errno = EIO;
                goto out;
        }

        /* get/refresh the in arg objects inode in correlation to the xlator */
        inode = glfs_resolve_inode (fs, subvol, object);
        if (!inode) {
                errno = ESTALE;
                goto out;
        }

        /* check types to open */
        if (IA_ISDIR (inode->ia_type)) {
                ret = -1;
                errno = EISDIR;
                goto out;
        }

        if (!IA_ISREG (inode->ia_type)) {
                ret = -1;
                errno = EINVAL;
                goto out;
        }

        glfd = glfs_fd_new (fs);
        if (!glfd) {
                ret = -1;
                errno = ENOMEM;
                goto out;
        }

        glfd->fd = fd_create (inode, getpid());
        if (!glfd->fd) {
                ret = -1;
                errno = ENOMEM;
                goto out;
        }
        glfd->fd->flags = flags;

        /* populate loc */
        GLFS_LOC_FILL_INODE (inode, loc, out);

        /* fop/op */
        ret = syncop_open (subvol, &loc, flags, glfd->fd, NULL, NULL);
        DECODE_SYNCOP_ERR (ret);

        glfd->fd->flags = flags;
        fd_bind (glfd->fd);
        glfs_fd_bind (glfd);

out:
        loc_wipe (&loc);

        if (inode)
                inode_unref (inode);

        if (ret && glfd) {
                GF_REF_PUT (glfd);
                glfd = NULL;
        } else if (glfd) {
                glfd->state = GLFD_OPEN;
        }

        glfs_subvol_done (fs, subvol);

        __GLFS_EXIT_FS;

invalid_fs:
        return glfd;
}

GFAPI_SYMVER_PUBLIC_DEFAULT(glfs_h_open, 3.4.2);


struct glfs_object *
pub_glfs_h_creat (struct glfs *fs, struct glfs_object *parent, const char *path,
                  int flags, mode_t mode, struct stat *stat)
{
        int                 ret = -1;
        fd_t               *fd = NULL;
        xlator_t           *subvol = NULL;
        inode_t            *inode = NULL;
        loc_t               loc = {0, };
        struct iatt         iatt = {0, };
        uuid_t              gfid;
        dict_t             *xattr_req = NULL;
        struct glfs_object *object = NULL;

        /* validate in args */
        if ((fs == NULL) || (parent == NULL) || (path == NULL)) {
                errno = EINVAL;
                return NULL;
        }

        DECLARE_OLD_THIS;
        __GLFS_ENTRY_VALIDATE_FS (fs, invalid_fs);

        /* get the active volume */
        subvol = glfs_active_subvol (fs);
        if (!subvol) {
                ret = -1;
                errno = EIO;
                goto out;
        }

        /* get/refresh the in arg objects inode in correlation to the xlator */
        inode = glfs_resolve_inode (fs, subvol, parent);
        if (!inode) {
                ret = -1;
                errno = ESTALE;
                goto out;
        }

        xattr_req = dict_new ();
        if (!xattr_req) {
                ret = -1;
                errno = ENOMEM;
                goto out;
        }

        gf_uuid_generate (gfid);
        ret = dict_set_static_bin (xattr_req, "gfid-req", gfid, 16);
        if (ret) {
                ret = -1;
                errno = ENOMEM;
                goto out;
        }

        GLFS_LOC_FILL_PINODE (inode, loc, ret, errno, out, path);

        fd = fd_create (loc.inode, getpid());
        if (!fd) {
                ret = -1;
                errno = ENOMEM;
                goto out;
        }
        fd->flags = flags;

        /* fop/op */
        ret = syncop_create (subvol, &loc, flags, mode, fd, &iatt,
                             xattr_req, NULL);
        DECODE_SYNCOP_ERR (ret);

        /* populate out args */
        if (ret == 0) {
                ret = glfs_loc_link (&loc, &iatt);
                if (ret != 0) {
                        goto out;
                }

                if (stat)
                        glfs_iatt_to_stat (fs, &iatt, stat);

                ret = glfs_create_object (&loc, &object);
        }

out:
        if (ret && object != NULL) {
                /* Release the held reference */
                glfs_h_close (object);
                object = NULL;
        }

        loc_wipe(&loc);

        if (inode)
                inode_unref (inode);

        if (xattr_req)
                dict_unref (xattr_req);

        if (fd)
                fd_unref(fd);

        glfs_subvol_done (fs, subvol);

        __GLFS_EXIT_FS;

invalid_fs:
        return object;
}

GFAPI_SYMVER_PUBLIC_DEFAULT(glfs_h_creat, 3.4.2);


struct glfs_object *
pub_glfs_h_mkdir (struct glfs *fs, struct glfs_object *parent, const char *path,
                  mode_t mode, struct stat *stat)
{
        int                 ret = -1;
        xlator_t           *subvol = NULL;
        inode_t            *inode = NULL;
        loc_t               loc = {0, };
        struct iatt         iatt = {0, };
        uuid_t              gfid;
        dict_t             *xattr_req = NULL;
        struct glfs_object *object = NULL;

        /* validate in args */
        if ((fs == NULL) || (parent == NULL) || (path == NULL)) {
                errno = EINVAL;
                return NULL;
        }

        DECLARE_OLD_THIS;
        __GLFS_ENTRY_VALIDATE_FS (fs, invalid_fs);

        /* get the active volume */
        subvol = glfs_active_subvol (fs);
        if (!subvol) {
                ret = -1;
                errno = EIO;
                goto out;
        }

        /* get/refresh the in arg objects inode in correlation to the xlator */
        inode = glfs_resolve_inode (fs, subvol, parent);
        if (!inode) {
                errno = ESTALE;
                goto out;
        }

        xattr_req = dict_new ();
        if (!xattr_req) {
                ret = -1;
                errno = ENOMEM;
                goto out;
        }

        gf_uuid_generate (gfid);
        ret = dict_set_static_bin (xattr_req, "gfid-req", gfid, 16);
        if (ret) {
                ret = -1;
                errno = ENOMEM;
                goto out;
        }

        GLFS_LOC_FILL_PINODE (inode, loc, ret, errno, out, path);

        /* fop/op */
        ret = syncop_mkdir (subvol, &loc, mode, &iatt, xattr_req, NULL);
        DECODE_SYNCOP_ERR (ret);

        /* populate out args */
        if ( ret == 0 )  {
                ret = glfs_loc_link (&loc, &iatt);
                if (ret != 0) {
                        goto out;
                }

                if (stat)
                        glfs_iatt_to_stat (fs, &iatt, stat);

                ret = glfs_create_object (&loc, &object);
        }

out:
        if (ret && object != NULL) {
                glfs_h_close (object);
                object = NULL;
        }

        loc_wipe(&loc);

        if (inode)
                inode_unref (inode);

        if (xattr_req)
                dict_unref (xattr_req);

        glfs_subvol_done (fs, subvol);

        __GLFS_EXIT_FS;

invalid_fs:
        return object;
}

GFAPI_SYMVER_PUBLIC_DEFAULT(glfs_h_mkdir, 3.4.2);


struct glfs_object *
pub_glfs_h_mknod (struct glfs *fs, struct glfs_object *parent, const char *path,
                  mode_t mode, dev_t dev, struct stat *stat)
{
        int                 ret = -1;
        xlator_t           *subvol = NULL;
        inode_t            *inode = NULL;
        loc_t               loc = {0, };
        struct iatt         iatt = {0, };
        uuid_t              gfid;
        dict_t             *xattr_req = NULL;
        struct glfs_object *object = NULL;

        /* validate in args */
        if ((fs == NULL) || (parent == NULL) || (path == NULL)) {
                errno = EINVAL;
                return NULL;
        }

        DECLARE_OLD_THIS;
        __GLFS_ENTRY_VALIDATE_FS (fs, invalid_fs);

        /* get the active volume */
        subvol = glfs_active_subvol (fs);
        if (!subvol) {
                ret = -1;
                errno = EIO;
                goto out;
        }

        /* get/refresh the in arg objects inode in correlation to the xlator */
        inode = glfs_resolve_inode (fs, subvol, parent);
        if (!inode) {
                errno = ESTALE;
                goto out;
        }

        xattr_req = dict_new ();
        if (!xattr_req) {
                ret = -1;
                errno = ENOMEM;
                goto out;
        }

        gf_uuid_generate (gfid);
        ret = dict_set_static_bin (xattr_req, "gfid-req", gfid, 16);
        if (ret) {
                ret = -1;
                errno = ENOMEM;
                goto out;
        }

        GLFS_LOC_FILL_PINODE (inode, loc, ret, errno, out, path);

        /* fop/op */
        ret = syncop_mknod (subvol, &loc, mode, dev, &iatt, xattr_req, NULL);
        DECODE_SYNCOP_ERR (ret);

        /* populate out args */
        if (ret == 0) {
                ret = glfs_loc_link (&loc, &iatt);
                if (ret != 0) {
                        goto out;
                }

                if (stat)
                        glfs_iatt_to_stat (fs, &iatt, stat);

                ret = glfs_create_object (&loc, &object);
        }
out:
        if (ret && object != NULL) {
                glfs_h_close (object);
                object = NULL;
        }

        loc_wipe(&loc);

        if (inode)
                inode_unref (inode);

        if (xattr_req)
                dict_unref (xattr_req);

        glfs_subvol_done (fs, subvol);

        __GLFS_EXIT_FS;

invalid_fs:
        return object;
}

GFAPI_SYMVER_PUBLIC_DEFAULT(glfs_h_mknod, 3.4.2);


int
pub_glfs_h_unlink (struct glfs *fs, struct glfs_object *parent, const char *path)
{
        int                 ret = -1;
        xlator_t           *subvol = NULL;
        inode_t            *inode = NULL;
        loc_t               loc = {0, };

        /* validate in args */
        if ((fs == NULL) || (parent == NULL) || (path == NULL)) {
                errno = EINVAL;
                return -1;
        }

        DECLARE_OLD_THIS;
        __GLFS_ENTRY_VALIDATE_FS (fs, invalid_fs);

        /* get the active volume */
        subvol = glfs_active_subvol (fs);
        if ( !subvol ) {
                ret = -1;
                errno = EIO;
                goto out;
        }

        /* get/refresh the in arg objects inode in correlation to the xlator */
        inode = glfs_resolve_inode (fs, subvol, parent);
        if (!inode) {
                errno = ESTALE;
                goto out;
        }

        ret = glfs_resolve_at (fs, subvol, inode, path, &loc, NULL, 0 , 0);
        if (ret != 0) {
                goto out;
        }

        if (!IA_ISDIR(loc.inode->ia_type)) {
                ret = syncop_unlink (subvol, &loc, NULL, NULL);
                DECODE_SYNCOP_ERR (ret);
                if (ret != 0) {
                        goto out;
                }
        } else {
                ret = syncop_rmdir (subvol, &loc, 0, NULL, NULL);
                DECODE_SYNCOP_ERR (ret);
                if (ret != 0) {
                        goto out;
                }
        }

        if (ret == 0)
                ret = glfs_loc_unlink (&loc);

out:
        loc_wipe (&loc);

        if (inode)
                inode_unref (inode);

        glfs_subvol_done (fs, subvol);

        __GLFS_EXIT_FS;

invalid_fs:
        return ret;
}

GFAPI_SYMVER_PUBLIC_DEFAULT(glfs_h_unlink, 3.4.2);


struct glfs_fd *
pub_glfs_h_opendir (struct glfs *fs, struct glfs_object *object)
{
        int              ret = -1;
        struct glfs_fd  *glfd = NULL;
        xlator_t        *subvol = NULL;
        inode_t         *inode = NULL;
        loc_t            loc = {0, };

        /* validate in args */
        if ((fs == NULL) || (object == NULL)) {
                errno = EINVAL;
                return NULL;
        }

        DECLARE_OLD_THIS;
        __GLFS_ENTRY_VALIDATE_FS (fs, invalid_fs);

        /* get the active volume */
        subvol = glfs_active_subvol (fs);
        if (!subvol) {
                ret = -1;
                errno = EIO;
                goto out;
        }

        /* get/refresh the in arg objects inode in correlation to the xlator */
        inode = glfs_resolve_inode (fs, subvol, object);
        if (!inode) {
                errno = ESTALE;
                goto out;
        }

        if (!IA_ISDIR (inode->ia_type)) {
                ret = -1;
                errno = ENOTDIR;
                goto out;
        }

        glfd = glfs_fd_new (fs);
        if (!glfd)
                goto out;

        INIT_LIST_HEAD (&glfd->entries);

        glfd->fd = fd_create (inode, getpid());
        if (!glfd->fd) {
                ret = -1;
                errno = ENOMEM;
                goto out;
        }

        GLFS_LOC_FILL_INODE (inode, loc, out);

        /* fop/op */
        ret = syncop_opendir (subvol, &loc, glfd->fd, NULL, NULL);
        DECODE_SYNCOP_ERR (ret);

out:
        loc_wipe (&loc);

        if (inode)
                inode_unref (inode);

        if (ret && glfd) {
                GF_REF_PUT (glfd);
                glfd = NULL;
        } else if (glfd) {
                glfd->state = GLFD_OPEN;
                fd_bind (glfd->fd);
                glfs_fd_bind (glfd);
        }

        glfs_subvol_done (fs, subvol);

        __GLFS_EXIT_FS;

invalid_fs:
        return glfd;
}

GFAPI_SYMVER_PUBLIC_DEFAULT(glfs_h_opendir, 3.4.2);


int
pub_glfs_h_access (struct glfs *fs, struct glfs_object *object, int mask)
{
	int              ret = -1;
	xlator_t        *subvol = NULL;
	inode_t         *inode = NULL;
	loc_t            loc = {0, };

	DECLARE_OLD_THIS;

	/* validate in args */
	if ((fs == NULL) || (object == NULL)) {
		errno = EINVAL;
		return ret;
	}

        __GLFS_ENTRY_VALIDATE_FS (fs, invalid_fs);

	/* get the active volume */
	subvol = glfs_active_subvol (fs);
	if (!subvol) {
		ret = -1;
		errno = EIO;
		goto out;
	}

	/* get/refresh the in arg objects inode in correlation to the xlator */
	inode = glfs_resolve_inode (fs, subvol, object);
	if (!inode) {
		errno = ESTALE;
		goto out;
	}


	GLFS_LOC_FILL_INODE (inode, loc, out);

	/* fop/op */

	ret = syncop_access (subvol, &loc, mask, NULL, NULL);
        DECODE_SYNCOP_ERR (ret);

out:
	loc_wipe (&loc);

	if (inode)
		inode_unref (inode);


	glfs_subvol_done (fs, subvol);

        __GLFS_EXIT_FS;

invalid_fs:
	return ret;
}

GFAPI_SYMVER_PUBLIC_DEFAULT(glfs_h_access, 3.6.0);


ssize_t
pub_glfs_h_extract_handle (struct glfs_object *object, unsigned char *handle,
                           int len)
{
        ssize_t ret = -1;

        /* validate in args */
        if (object == NULL) {
                errno = EINVAL;
                goto out;
        }

        if (!handle || !len) {
                ret = GFAPI_HANDLE_LENGTH;
                goto out;
        }

        if (len < GFAPI_HANDLE_LENGTH)
        {
                errno = ERANGE;
                goto out;
        }

        memcpy (handle, object->gfid, GFAPI_HANDLE_LENGTH);

        ret = GFAPI_HANDLE_LENGTH;

out:
        return ret;
}

GFAPI_SYMVER_PUBLIC_DEFAULT(glfs_h_extract_handle, 3.4.2);


struct glfs_object *
pub_glfs_h_create_from_handle (struct glfs *fs, unsigned char *handle, int len,
                               struct stat *stat)
{
        loc_t               loc = {0, };
        int                 ret = -1;
        struct iatt         iatt = {0, };
        inode_t            *newinode = NULL;
        xlator_t           *subvol = NULL;
        struct glfs_object *object = NULL;
        uint64_t            ctx_value = LOOKUP_NOT_NEEDED;

        /* validate in args */
        if ((fs == NULL) || (handle == NULL) || (len != GFAPI_HANDLE_LENGTH)) {
                errno = EINVAL;
                return NULL;
        }

        DECLARE_OLD_THIS;
        __GLFS_ENTRY_VALIDATE_FS (fs, invalid_fs);

        /* get the active volume */
        subvol = glfs_active_subvol (fs);
        if (!subvol) {
                errno = EIO;
                goto out;
        }

        memcpy (loc.gfid, handle, GFAPI_HANDLE_LENGTH);

        /* make sure the gfid received is valid */
        GF_VALIDATE_OR_GOTO ("glfs_h_create_from_handle",
                             !(gf_uuid_is_null (loc.gfid)), out);

        newinode = inode_find (subvol->itable, loc.gfid);
        if (newinode) {
                if (!stat) /* No need of lookup */
                        goto found;

                loc.inode = newinode;
        } else {
                loc.inode = inode_new (subvol->itable);
                if (!loc.inode) {
                        errno = ENOMEM;
                        goto out;
                }
        }

        ret = syncop_lookup (subvol, &loc, &iatt, 0, 0, 0);
        DECODE_SYNCOP_ERR (ret);
        if (ret) {
                gf_msg (subvol->name, GF_LOG_WARNING, errno,
                        API_MSG_INODE_REFRESH_FAILED,
                        "inode refresh of %s failed: %s",
                        uuid_utoa (loc.gfid), strerror (errno));
                goto out;
        }

        newinode = inode_link (loc.inode, 0, 0, &iatt);
        if (newinode) {
                if (newinode == loc.inode) {
                        inode_ctx_set (newinode, THIS, &ctx_value);
                }
                inode_lookup (newinode);
        } else {
                gf_msg (subvol->name, GF_LOG_WARNING, errno,
                        API_MSG_INODE_LINK_FAILED,
                        "inode linking of %s failed", uuid_utoa (loc.gfid));
                goto out;
        }

        /* populate stat */
        if (stat)
                glfs_iatt_to_stat (fs, &iatt, stat);

found:
        object = GF_CALLOC (1, sizeof(struct glfs_object),
                            glfs_mt_glfs_object_t);
        if (object == NULL) {
                errno = ENOMEM;
                ret = -1;
                goto out;
        }

        /* populate the return object */
        object->inode = newinode;
        gf_uuid_copy (object->gfid, object->inode->gfid);

out:
        /* TODO: Check where the inode ref is being held? */
        loc_wipe (&loc);

        glfs_subvol_done (fs, subvol);

        __GLFS_EXIT_FS;

invalid_fs:
        return object;
}

GFAPI_SYMVER_PUBLIC_DEFAULT(glfs_h_create_from_handle, 3.4.2);


int
pub_glfs_h_close (struct glfs_object *object)
{
        /* since glfs_h_* objects hold a reference to inode
         * it is safe to keep lookup count to '0' */
        inode_forget (object->inode, 0);
        inode_unref (object->inode);
        GF_FREE (object);

        return 0;
}

GFAPI_SYMVER_PUBLIC_DEFAULT(glfs_h_close, 3.4.2);


int
pub_glfs_h_truncate (struct glfs *fs, struct glfs_object *object, off_t offset)
{
        loc_t               loc = {0, };
        int                 ret = -1;
        xlator_t           *subvol = NULL;
        inode_t            *inode = NULL;

        DECLARE_OLD_THIS;

        /* validate in args */
        if (object == NULL) {
                errno = EINVAL;
                return -1;
        }

        __GLFS_ENTRY_VALIDATE_FS (fs, invalid_fs);

        /* get the active volume */
        subvol = glfs_active_subvol (fs);
        if (!subvol) {
                ret = -1;
                errno = EIO;
                goto out;
        }

        /* get/refresh the in arg objects inode in correlation to the xlator */
        inode = glfs_resolve_inode (fs, subvol, object);
        if (!inode) {
                errno = ESTALE;
                goto out;
        }

        GLFS_LOC_FILL_INODE (inode, loc, out);

        /* fop/op */
        ret = syncop_truncate (subvol, &loc, (off_t)offset, NULL, NULL);
        DECODE_SYNCOP_ERR (ret);

        /* populate out args */
        if (ret == 0)
                ret = glfs_loc_unlink (&loc);

out:
        loc_wipe (&loc);

        if (inode)
                inode_unref (inode);

        glfs_subvol_done (fs, subvol);

        __GLFS_EXIT_FS;

invalid_fs:
        return ret;
}

GFAPI_SYMVER_PUBLIC_DEFAULT(glfs_h_truncate, 3.4.2);


struct glfs_object *
pub_glfs_h_symlink (struct glfs *fs, struct glfs_object *parent,
                    const char *name, const char *data, struct stat *stat)
{
        int                 ret = -1;
        xlator_t           *subvol = NULL;
        inode_t            *inode = NULL;
        loc_t               loc = {0, };
        struct iatt         iatt = {0, };
        uuid_t              gfid;
        dict_t             *xattr_req = NULL;
        struct glfs_object *object = NULL;

        DECLARE_OLD_THIS;

        /* validate in args */
        if ((parent == NULL) || (name == NULL) ||
                (data == NULL)) {
                errno = EINVAL;
                return NULL;
        }

        __GLFS_ENTRY_VALIDATE_FS (fs, invalid_fs);

        /* get the active volume */
        subvol = glfs_active_subvol (fs);
        if (!subvol) {
                ret = -1;
                errno = EIO;
                goto out;
        }

        /* get/refresh the in arg objects inode in correlation to the xlator */
        inode = glfs_resolve_inode (fs, subvol, parent);
        if (!inode) {
                errno = ESTALE;
                goto out;
        }

        xattr_req = dict_new ();
        if (!xattr_req) {
                ret = -1;
                errno = ENOMEM;
                goto out;
        }

        gf_uuid_generate (gfid);
        ret = dict_set_static_bin (xattr_req, "gfid-req", gfid, 16);
        if (ret) {
                ret = -1;
                errno = ENOMEM;
                goto out;
        }

        GLFS_LOC_FILL_PINODE (inode, loc, ret, errno, out, name);

        /* fop/op */
        ret = syncop_symlink (subvol, &loc, data, &iatt, xattr_req, NULL);
        DECODE_SYNCOP_ERR (ret);

        /* populate out args */
        if (ret == 0) {
                ret = glfs_loc_link (&loc, &iatt);
                if (ret != 0) {
                        goto out;
                }

                if (stat)
                        glfs_iatt_to_stat (fs, &iatt, stat);

                ret = glfs_create_object (&loc, &object);
        }

out:
        if (ret && object != NULL) {
                pub_glfs_h_close (object);
                object = NULL;
        }

        loc_wipe(&loc);

        if (inode)
                inode_unref (inode);

        if (xattr_req)
                dict_unref (xattr_req);

        glfs_subvol_done (fs, subvol);

        __GLFS_EXIT_FS;

invalid_fs:
        return object;
}

GFAPI_SYMVER_PUBLIC_DEFAULT(glfs_h_symlink, 3.4.2);


int
pub_glfs_h_readlink (struct glfs *fs, struct glfs_object *object, char *buf,
                     size_t bufsiz)
{
        loc_t               loc = {0, };
        int                 ret = -1;
        xlator_t           *subvol = NULL;
        inode_t            *inode = NULL;
        char               *linkval = NULL;

        DECLARE_OLD_THIS;

        /* validate in args */
        if ((object == NULL) || (buf == NULL)) {
                errno = EINVAL;
                return -1;
        }

        __GLFS_ENTRY_VALIDATE_FS (fs, invalid_fs);

        /* get the active volume */
        subvol = glfs_active_subvol (fs);
        if (!subvol) {
                ret = -1;
                errno = EIO;
                goto out;
        }

        /* get/refresh the in arg objects inode in correlation to the xlator */
        inode = glfs_resolve_inode (fs, subvol, object);
        if (!inode) {
                errno = ESTALE;
                goto out;
        }

        GLFS_LOC_FILL_INODE (inode, loc, out);

        /* fop/op */
        ret = syncop_readlink (subvol, &loc, &linkval, bufsiz, NULL, NULL);
        DECODE_SYNCOP_ERR (ret);

        /* populate out args */
        if (ret > 0)
                memcpy (buf, linkval, ret);

out:
        loc_wipe (&loc);

        if (inode)
                inode_unref (inode);

        if (linkval)
                GF_FREE (linkval);

        glfs_subvol_done (fs, subvol);

        __GLFS_EXIT_FS;

invalid_fs:
        return ret;
}

GFAPI_SYMVER_PUBLIC_DEFAULT(glfs_h_readlink, 3.4.2);


int
pub_glfs_h_link (struct glfs *fs, struct glfs_object *linksrc,
             struct glfs_object *parent, const char *name)
{
        int                 ret = -1;
        xlator_t           *subvol = NULL;
        inode_t            *inode = NULL;
        inode_t            *pinode = NULL;
        loc_t               oldloc = {0, };
        loc_t               newloc = {0, };
        struct iatt         iatt = {0, };

        DECLARE_OLD_THIS;

        /* validate in args */
        if ((linksrc == NULL) || (parent == NULL) ||
                (name == NULL)) {
                errno = EINVAL;
                return -1;
        }

        __GLFS_ENTRY_VALIDATE_FS (fs, invalid_fs);

        /* get the active volume */
        subvol = glfs_active_subvol (fs);
        if (!subvol) {
                ret = -1;
                errno = EIO;
                goto out;
        }

        /* get/refresh the in arg objects inode in correlation to the xlator */
        inode = glfs_resolve_inode (fs, subvol, linksrc);
        if (!inode) {
                errno = ESTALE;
                goto out;
        }

        if (inode->ia_type == IA_IFDIR) {
                ret = -1;
                errno = EISDIR;
                goto out;
        }

        GLFS_LOC_FILL_INODE (inode, oldloc, out);

        /* get/refresh the in arg objects inode in correlation to the xlator */
        pinode = glfs_resolve_inode (fs, subvol, parent);
        if (!pinode) {
                errno = ESTALE;
                goto out;
        }

        /* setup newloc based on parent */
        newloc.parent = inode_ref (pinode);
        newloc.name = name;
        ret = glfs_loc_touchup (&newloc);
        if (ret != 0) {
                errno = EINVAL;
                goto out;
        }

        /* Filling the inode of the hard link to be same as that of the
         * original file
         */
        newloc.inode = inode_ref (inode);

        /* fop/op */
        ret = syncop_link (subvol, &oldloc, &newloc, &iatt, NULL, NULL);
        DECODE_SYNCOP_ERR (ret);

        if (ret == 0)
                ret = glfs_loc_link (&newloc, &iatt);
out:
        loc_wipe (&oldloc);
        loc_wipe (&newloc);

        if (inode)
                inode_unref (inode);

        if (pinode)
                inode_unref (pinode);

        glfs_subvol_done (fs, subvol);

        __GLFS_EXIT_FS;

invalid_fs:
        return ret;
}

GFAPI_SYMVER_PUBLIC_DEFAULT(glfs_h_link, 3.4.2);


int
pub_glfs_h_rename (struct glfs *fs, struct glfs_object *olddir,
                   const char *oldname, struct glfs_object *newdir,
                   const char *newname)
{
        int                 ret = -1;
        xlator_t           *subvol = NULL;
        inode_t            *oldpinode = NULL;
        inode_t            *newpinode = NULL;
        loc_t               oldloc = {0, };
        loc_t               newloc = {0, };
        struct iatt         oldiatt = {0, };
        struct iatt         newiatt = {0, };

        DECLARE_OLD_THIS;

        /* validate in args */
        if ((olddir == NULL) || (oldname == NULL) ||
                (newdir == NULL) || (newname == NULL)) {
                errno = EINVAL;
                return -1;
        }

        __GLFS_ENTRY_VALIDATE_FS (fs, invalid_fs);

        /* get the active volume */
        subvol = glfs_active_subvol (fs);
        if ( !subvol ) {
                ret = -1;
                errno = EIO;
                goto out;
        }

        /* get/refresh the in arg objects inode in correlation to the xlator */
        oldpinode = glfs_resolve_inode (fs, subvol, olddir);
        if (!oldpinode) {
                errno = ESTALE;
                goto out;
        }

        ret = glfs_resolve_at (fs, subvol, oldpinode, oldname, &oldloc,
                                    &oldiatt, 0 , 0);
        if (ret != 0) {
                goto out;
        }

        /* get/refresh the in arg objects inode in correlation to the xlator */
        newpinode = glfs_resolve_inode (fs, subvol, newdir);
        if (!newpinode) {
                errno = ESTALE;
                goto out;
        }

        ret = glfs_resolve_at (fs, subvol, newpinode, newname, &newloc,
                                    &newiatt, 0, 0);

        if (ret && errno != ENOENT && newloc.parent)
                goto out;

        if (newiatt.ia_type != IA_INVAL) {
                if ((oldiatt.ia_type == IA_IFDIR) !=
                        (newiatt.ia_type == IA_IFDIR)) {
                        /* Either both old and new must be dirs,
                         * or both must be non-dirs. Else, fail.
                         */
                        ret = -1;
                        errno = EEXIST;
                        goto out;
                }
        }

        /* TODO: check if new or old is a prefix of the other, and fail EINVAL */

        ret = syncop_rename (subvol, &oldloc, &newloc, NULL, NULL);
        DECODE_SYNCOP_ERR (ret);

        if (ret == 0)
                inode_rename (oldloc.parent->table, oldloc.parent, oldloc.name,
                              newloc.parent, newloc.name, oldloc.inode,
                              &oldiatt);

out:
        loc_wipe (&oldloc);
        loc_wipe (&newloc);

        if (oldpinode)
                inode_unref (oldpinode);

        if (newpinode)
                inode_unref (newpinode);

        glfs_subvol_done (fs, subvol);

        __GLFS_EXIT_FS;

invalid_fs:
        return ret;
}

GFAPI_SYMVER_PUBLIC_DEFAULT(glfs_h_rename, 3.4.2);

/*
 * Given a handle/gfid, find if the corresponding inode is present in
 * the inode table. If yes create and return the corresponding glfs_object.
 */
struct glfs_object *
glfs_h_find_handle (struct glfs *fs, unsigned char *handle, int len)
{
        inode_t            *newinode = NULL;
        xlator_t           *subvol = NULL;
        struct glfs_object *object = NULL;
        uuid_t gfid;

        /* validate in args */
        if ((fs == NULL) || (handle == NULL) || (len != GFAPI_HANDLE_LENGTH)) {
                errno = EINVAL;
                return NULL;
        }

        DECLARE_OLD_THIS;
        __GLFS_ENTRY_VALIDATE_FS (fs, invalid_fs);

        /* get the active volume */
        subvol = glfs_active_subvol (fs);
        if (!subvol) {
                errno = EIO;
                goto out;
        }

        memcpy (gfid, handle, GFAPI_HANDLE_LENGTH);

        /* make sure the gfid received is valid */
        GF_VALIDATE_OR_GOTO ("glfs_h_find_handle",
                             !(gf_uuid_is_null (gfid)), out);

        newinode = inode_find (subvol->itable, gfid);
        if (!newinode) {
                goto out;
        }

        object = GF_CALLOC (1, sizeof(struct glfs_object),
                            glfs_mt_glfs_object_t);
        if (object == NULL) {
                errno = ENOMEM;
                goto out;
        }

        /* populate the return object. The ref taken here
         * is un'refed when the application does glfs_h_close() */
        object->inode = inode_ref(newinode);
        gf_uuid_copy (object->gfid, object->inode->gfid);

out:
        /* inode_find takes a reference. Unref it. */
        if (newinode)
                inode_unref (newinode);

        glfs_subvol_done (fs, subvol);

        __GLFS_EXIT_FS;

invalid_fs:
        return object;

}

static void
glfs_free_upcall_inode (void *to_free)
{
        struct glfs_upcall_inode *arg = to_free;

        if (!arg)
                return;

        if (arg->object)
                glfs_h_close (arg->object);
        if (arg->p_object)
                glfs_h_close (arg->p_object);
        if (arg->oldp_object)
                glfs_h_close (arg->oldp_object);

        GF_FREE (arg);
}

int
glfs_h_poll_cache_invalidation (struct glfs *fs,
                                struct glfs_upcall *up_arg,
                                struct gf_upcall *upcall_data)
{
        int                                 ret           = -1;
        struct glfs_object                  *p_object     = NULL;
        struct glfs_object                  *oldp_object  = NULL;
        struct glfs_object                  *object       = NULL;
        struct gf_upcall_cache_invalidation *ca_data      = NULL;
        struct glfs_upcall_inode            *up_inode_arg = NULL;

        ca_data = upcall_data->data;
        GF_VALIDATE_OR_GOTO ("glfs_h_poll_cache_invalidation",
                             ca_data, out);

        object = glfs_h_find_handle (fs, upcall_data->gfid,
                                     GFAPI_HANDLE_LENGTH);
        if (!object) {
                /* The reason handle creation will fail is because we
                 * couldn't find the inode in the gfapi inode table.
                 *
                 * But since application would have taken inode_ref, the
                 * only case when this can happen is when it has closed
                 * the handle and hence will no more be interested in
                 * the upcall for this particular gfid.
                 */
                gf_msg (THIS->name, GF_LOG_DEBUG, errno,
                        API_MSG_CREATE_HANDLE_FAILED,
                        "handle creation of %s failed",
                         uuid_utoa (upcall_data->gfid));
                errno = ESTALE;
                goto out;
        }

        up_inode_arg = GF_CALLOC (1, sizeof (struct glfs_upcall_inode),
                                  glfs_mt_upcall_inode_t);
        GF_VALIDATE_OR_GOTO ("glfs_h_poll_cache_invalidation",
                             up_inode_arg, out);

        up_inode_arg->object = object;
        up_inode_arg->flags = ca_data->flags;
        up_inode_arg->expire_time_attr = ca_data->expire_time_attr;

        /* XXX: Update stat as well incase of UP_*_TIMES.
         * This will be addressed as part of INODE_UPDATE */
        if (ca_data->flags & GFAPI_INODE_UPDATE_FLAGS) {
                glfs_iatt_to_stat (fs, &ca_data->stat, &up_inode_arg->buf);
        }

        if (ca_data->flags & GFAPI_UP_PARENT_TIMES) {
                p_object = glfs_h_find_handle (fs,
                                               ca_data->p_stat.ia_gfid,
                                               GFAPI_HANDLE_LENGTH);
                if (!p_object) {
                        gf_msg (THIS->name, GF_LOG_DEBUG, errno,
                                API_MSG_CREATE_HANDLE_FAILED,
                                "handle creation of %s failed",
                                 uuid_utoa (ca_data->p_stat.ia_gfid));
                        errno = ESTALE;
                        goto out;
                }

                glfs_iatt_to_stat (fs, &ca_data->p_stat, &up_inode_arg->p_buf);
        }
        up_inode_arg->p_object = p_object;

        /* In case of RENAME, update old parent as well */
        if (ca_data->flags & GFAPI_UP_RENAME) {
                oldp_object = glfs_h_find_handle (fs,
                                                  ca_data->oldp_stat.ia_gfid,
                                                  GFAPI_HANDLE_LENGTH);
                if (!oldp_object) {
                        gf_msg (THIS->name, GF_LOG_DEBUG, errno,
                                API_MSG_CREATE_HANDLE_FAILED,
                                "handle creation of %s failed",
                                 uuid_utoa (ca_data->oldp_stat.ia_gfid));
                        errno = ESTALE;
                        /* By the time we receive upcall old parent_dir may
                         * have got removed. We still need to send upcall
                         * for the file/dir and current parent handles. */
                        up_inode_arg->oldp_object = NULL;
                        ret = 0;
                }

                glfs_iatt_to_stat (fs, &ca_data->oldp_stat,
                                   &up_inode_arg->oldp_buf);
        }
        up_inode_arg->oldp_object = oldp_object;

        up_arg->reason = GLFS_UPCALL_INODE_INVALIDATE;
        up_arg->event = up_inode_arg;
        up_arg->free_event = glfs_free_upcall_inode;

        ret = 0;

out:
        if (ret) {
                /* Close p_object and oldp_object as well if being referenced.*/
                if (object)
                        glfs_h_close (object);

                /* Set reason to prevent applications from using ->event */
                up_arg->reason = GLFS_UPCALL_EVENT_NULL;
                GF_FREE (up_inode_arg);
        }
        return ret;
}

/*
 * This API is used to poll for upcall events stored in the upcall list.
 * Current users of this API is NFS-Ganesha. Incase of any event received, it
 * will be mapped appropriately into 'glfs_upcall' along with the handle object
 * to be passed to NFS-Ganesha.
 *
 * On success, applications need to check if up_arg is not-NULL or errno is not
 * ENOENT. glfs_upcall_get_reason() can be used to decide what kind of event
 * has been received.
 *
 * Current supported upcall_events:
 *      GLFS_UPCALL_INODE_INVALIDATE
 *
 * After processing the event, applications need to free 'up_arg' by calling
 * glfs_free().
 *
 * Also similar to I/Os, the application should ideally stop polling before
 * calling glfs_fini(..). Hence making an assumption that 'fs' & ctx structures
 * cannot be freed while in this routine.
 */
int
pub_glfs_h_poll_upcall (struct glfs *fs, struct glfs_upcall **up_arg)
{
        upcall_entry       *u_list         = NULL;
        upcall_entry       *tmp            = NULL;
        xlator_t           *subvol         = NULL;
        glusterfs_ctx_t    *ctx            = NULL;
        int                 ret            = -1;
        struct gf_upcall   *upcall_data    = NULL;

        DECLARE_OLD_THIS;

        if (!up_arg) {
                errno = EINVAL;
                goto err;
        }

        __GLFS_ENTRY_VALIDATE_FS (fs, err);

        /* get the active volume */
        subvol = glfs_active_subvol (fs);
        if (!subvol) {
                errno = EIO;
                goto restore;
        }

        /* Ideally applications should stop polling before calling
         * 'glfs_fini'. Yet cross check if cleanup has started. */
        pthread_mutex_lock (&fs->mutex);
        {
                ctx = fs->ctx;

                if (ctx->cleanup_started) {
                        pthread_mutex_unlock (&fs->mutex);
                        goto out;
                }

                fs->pin_refcnt++;

                /* once we call this function, the applications seems to be
                 * interested in events, enable caching them */
                fs->cache_upcalls = _gf_true;
        }
        pthread_mutex_unlock (&fs->mutex);

        pthread_mutex_lock (&fs->upcall_list_mutex);
        {
                list_for_each_entry_safe (u_list, tmp,
                                          &fs->upcall_list,
                                          upcall_list) {
                        list_del_init (&u_list->upcall_list);
                        upcall_data = &u_list->upcall_data;
                        break;
                }
        }
        /* No other thread can delete this entry. So unlock it */
        pthread_mutex_unlock (&fs->upcall_list_mutex);

        if (upcall_data) {
                switch (upcall_data->event_type) {
                case GF_UPCALL_CACHE_INVALIDATION:
                        *up_arg = GF_CALLOC (1, sizeof (struct gf_upcall),
                                             glfs_mt_upcall_entry_t);
                        if (!*up_arg) {
                                errno = ENOMEM;
                                break; /* goto free u_list */
                        }

                        /* XXX: Need to revisit this to support
                         * GLFS_UPCALL_INODE_UPDATE if required. */
                        ret = glfs_h_poll_cache_invalidation (fs, *up_arg,
                                                              upcall_data);
                        if (ret
                            || (*up_arg)->reason == GLFS_UPCALL_EVENT_NULL) {
                                /* It could so happen that the file which got
                                 * upcall notification may have got deleted by
                                 * the same client. Irrespective of the error,
                                 * return with an error or success+ENOENT. */
                                if ((*up_arg)->reason == GLFS_UPCALL_EVENT_NULL)
                                        errno = ENOENT;

                                GF_FREE (*up_arg);
                                *up_arg = NULL;
                        }
                        break;
                case GF_UPCALL_RECALL_LEASE:
                        gf_log ("glfs_h_poll_upcall", GF_LOG_DEBUG,
                                "UPCALL_RECALL_LEASE is not implemented yet");
                case GF_UPCALL_EVENT_NULL:
                /* no 'default:' label, to force handling all upcall events */
                        errno = ENOENT;
                        break;
                }

                GF_FREE (u_list->upcall_data.data);
                GF_FREE (u_list);
        } else {
                /* fs->upcall_list was empty, no upcall events cached */
                errno = ENOENT;
        }

        ret = 0;

out:
        pthread_mutex_lock (&fs->mutex);
        {
                fs->pin_refcnt--;
        }
        pthread_mutex_unlock (&fs->mutex);

        glfs_subvol_done (fs, subvol);

restore:
        __GLFS_EXIT_FS;
err:
        return ret;
}

GFAPI_SYMVER_PUBLIC_DEFAULT(glfs_h_poll_upcall, 3.7.16);

static gf_boolean_t log_upcall370 = _gf_true; /* log once */

/* The old glfs_h_poll_upcall interface requires intimite knowledge of the
 * structures that are returned to the calling application. This is not
 * recommended, as the returned structures need to returned correctly (handles
 * closed, memory free'd with the unavailable GF_FREE(), and possibly more.)
 *
 * To the best of our knowledge, only NFS-Ganesha uses the upcall events
 * through gfapi. We keep this backwards compatability function around so that
 * applications using the existing implementation do not break.
 *
 * WARNING: this function will be removed in the future.
 */
int
pub_glfs_h_poll_upcall370 (struct glfs *fs, struct glfs_callback_arg *up_arg)
{
        struct glfs_upcall    *upcall        = NULL;
        int                    ret           = -1;

        if (log_upcall370) {
                log_upcall370 = _gf_false;
                gf_log (THIS->name, GF_LOG_WARNING, "this application is "
                        "compiled against an old version of libgfapi, it "
                        "should use glfs_free() to release the structure "
                        "returned by glfs_h_poll_upcall() - for more details, "
                        "see http://review.gluster.org/14701");
        }

        ret = pub_glfs_h_poll_upcall (fs, &upcall);
        if (ret == 0) {
                up_arg->fs = fs;
                if (errno == ENOENT || upcall->event == NULL) {
                        up_arg->reason = GLFS_UPCALL_EVENT_NULL;
                        goto out;
                }

                up_arg->reason = upcall->reason;

                if (upcall->reason == GLFS_UPCALL_INODE_INVALIDATE) {
                        struct glfs_callback_inode_arg *cb_inode = NULL;
                        struct glfs_upcall_inode       *up_inode = NULL;

                        cb_inode = GF_CALLOC (1,
                                              sizeof (struct glfs_callback_inode_arg),
                                              glfs_mt_upcall_inode_t);
                        if (!cb_inode) {
                                errno = ENOMEM;
                                ret = -1;
                                goto out;
                        }

                        up_inode = upcall->event;

                        /* copy attributes one by one, the memory layout might
                         * be different between the old glfs_callback_inode_arg
                         * and new glfs_upcall_inode */
                        cb_inode->object = up_inode->object;
                        cb_inode->flags = up_inode->flags;
                        memcpy (&cb_inode->buf, &up_inode->buf,
                                sizeof (struct stat));
                        cb_inode->expire_time_attr = up_inode->expire_time_attr;
                        cb_inode->p_object = up_inode->p_object;
                        memcpy (&cb_inode->p_buf, &up_inode->p_buf,
                                sizeof (struct stat));
                        cb_inode->oldp_object = up_inode->oldp_object;
                        memcpy (&cb_inode->oldp_buf, &up_inode->oldp_buf,
                                sizeof (struct stat));

                        up_arg->event_arg = cb_inode;
                }
        }

out:
        if (upcall) {
                /* we can not use glfs_free() here, objects need to stay */
                GF_FREE (upcall->event);
                GF_FREE (upcall);
        }

        return ret;
}

GFAPI_SYMVER_PUBLIC(glfs_h_poll_upcall370, glfs_h_poll_upcall, 3.7.0);

#ifdef HAVE_ACL_LIBACL_H
#include "glusterfs-acl.h"
#include <acl/libacl.h>

int
pub_glfs_h_acl_set (struct glfs *fs, struct glfs_object *object,
                    const acl_type_t type, const acl_t acl)
{
        int ret = -1;
        char *acl_s = NULL;
        const char *acl_key = NULL;
        struct glfs_object *new_object = NULL;

        DECLARE_OLD_THIS;

        if (!object || !acl) {
                errno = EINVAL;
                return ret;
        }

        __GLFS_ENTRY_VALIDATE_FS (fs, invalid_fs);

        acl_key = gf_posix_acl_get_key (type);
        if (!acl_key)
                goto out;

        acl_s = acl_to_any_text (acl, NULL, ',',
                                 TEXT_ABBREVIATE | TEXT_NUMERIC_IDS);
        if (!acl_s)
                goto out;

        if (IA_ISLNK (object->inode->ia_type)) {
                new_object = glfs_h_resolve_symlink (fs, object);
                if (new_object == NULL)
                        goto out;
        } else
                new_object = object;

        ret = pub_glfs_h_setxattrs (fs, new_object, acl_key, acl_s,
                                    strlen (acl_s) + 1, 0);

        acl_free (acl_s);

out:
        if (IA_ISLNK (object->inode->ia_type) && new_object)
                glfs_h_close (new_object);

        __GLFS_EXIT_FS;

invalid_fs:
        return ret;
}

acl_t
pub_glfs_h_acl_get (struct glfs *fs, struct glfs_object *object,
                    const acl_type_t type)
{
        int                 ret = 0;
        acl_t acl = NULL;
        char *acl_s = NULL;
        dict_t *xattr = NULL;
        const char *acl_key = NULL;
        struct glfs_object *new_object = NULL;

        DECLARE_OLD_THIS;

        if (!object) {
                errno = EINVAL;
                return NULL;
        }

        __GLFS_ENTRY_VALIDATE_FS (fs, invalid_fs);

        acl_key = gf_posix_acl_get_key (type);
        if (!acl_key)
                goto out;

        if (IA_ISLNK (object->inode->ia_type)) {
                new_object = glfs_h_resolve_symlink (fs, object);
                if (new_object == NULL)
                        goto out;
        } else
                new_object = object;

        ret = glfs_h_getxattrs_common (fs, new_object, &xattr, acl_key,
                                       _gf_false);
        if (ret)
                goto out;

        ret = dict_get_str (xattr, (char *)acl_key, &acl_s);
        if (ret == -1)
                goto out;

        acl = acl_from_text (acl_s);

out:
        GF_FREE (acl_s);
        if (IA_ISLNK (object->inode->ia_type) && new_object)
                glfs_h_close (new_object);

        __GLFS_EXIT_FS;

invalid_fs:
        return acl;
}
#else /* !HAVE_ACL_LIBACL_H */
acl_t
pub_glfs_h_acl_get (struct glfs *fs, struct glfs_object *object,
                    const acl_type_t type)
{
        errno = ENOTSUP;
        return NULL;
}

int
pub_glfs_h_acl_set (struct glfs *fs, struct glfs_object *object,
                    const acl_type_t type, const acl_t acl)
{
        errno = ENOTSUP;
        return -1;
}
#endif
GFAPI_SYMVER_PUBLIC_DEFAULT(glfs_h_acl_set, 3.7.0);
GFAPI_SYMVER_PUBLIC_DEFAULT(glfs_h_acl_get, 3.7.0);

/* The API to perform read using anonymous fd */
ssize_t
pub_glfs_h_anonymous_read (struct glfs *fs, struct glfs_object *object,
                           const void *buf, size_t count, off_t offset)
{
        struct iovec    iov     = {0, };
        ssize_t         ret     = 0;

        /* validate in args */
        if ((fs == NULL) || (object == NULL)) {
                errno = EINVAL;
                return -1;
        }

        iov.iov_base = (void *) buf;
        iov.iov_len = count;

        ret = glfs_anonymous_preadv (fs, object, &iov, 1, offset, 0);

        return ret;
}

GFAPI_SYMVER_PUBLIC_DEFAULT(glfs_h_anonymous_read, 3.7.0);

/* The API to perform write using anonymous fd */
ssize_t
pub_glfs_h_anonymous_write (struct glfs *fs, struct glfs_object *object,
                            const void *buf, size_t count, off_t offset)
{
        struct iovec iov        = {0, };
        ssize_t      ret        = 0;

        /* validate in args */
        if ((fs == NULL) || (object == NULL)) {
                errno = EINVAL;
                return -1;
        }

        iov.iov_base = (void *) buf;
        iov.iov_len = count;

        ret = glfs_anonymous_pwritev (fs, object, &iov, 1, offset, 0);

        return ret;
}

GFAPI_SYMVER_PUBLIC_DEFAULT(glfs_h_anonymous_write, 3.7.0);
