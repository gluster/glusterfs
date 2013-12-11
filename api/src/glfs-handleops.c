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
glfs_h_lookupat (struct glfs *fs, struct glfs_object *parent,
		 const char *path, struct stat *stat)
{
	int                      ret = 0;
	xlator_t                *subvol = NULL;
	inode_t                 *inode = NULL;
	struct iatt              iatt = {0, };
	struct glfs_object      *object = NULL;
	loc_t                    loc = {0, };

	/* validate in args */
	if ((fs == NULL) || (path == NULL)) {
		errno = EINVAL;
		return NULL;
	}

	__glfs_entry_fs (fs);

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
			       0 /*TODO: links? */, 0);

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

	return object;
}

int
glfs_h_stat (struct glfs *fs, struct glfs_object *object, struct stat *stat)
{
	int              ret = -1;
	xlator_t        *subvol = NULL;
	inode_t         *inode = NULL;
	loc_t            loc = {0, };
	struct iatt      iatt = {0, };

	/* validate in args */
	if ((fs == NULL) || (object == NULL)) {
		errno = EINVAL;
		return -1;
	}

	__glfs_entry_fs (fs);

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
	ret = syncop_stat (subvol, &loc, &iatt);
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

	return ret;
}

int
glfs_h_getattrs (struct glfs *fs, struct glfs_object *object, struct stat *stat)
{
	int                      ret = 0;
	xlator_t                *subvol = NULL;
	inode_t                 *inode = NULL;
	struct iatt              iatt = {0, };

	/* validate in args */
	if ((fs == NULL) || (object == NULL)) {
		errno = EINVAL;
		return -1;
	}

	__glfs_entry_fs (fs);

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

	return ret;
}

int
glfs_h_setattrs (struct glfs *fs, struct glfs_object *object, struct stat *stat,
		 int valid)
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

	__glfs_entry_fs (fs);

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
	ret = syncop_setattr (subvol, &loc, &iatt, glvalid, 0, 0);
        DECODE_SYNCOP_ERR (ret);
out:
	loc_wipe (&loc);

	if (inode)
		inode_unref (inode);

	glfs_subvol_done (fs, subvol);

	return ret;
}

struct glfs_fd *
glfs_h_open (struct glfs *fs, struct glfs_object *object, int flags)
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

	__glfs_entry_fs (fs);

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
		errno = ENOMEM;
		goto out;
	}

	glfd->fd = fd_create (inode, getpid());
	if (!glfd->fd) {
		ret = -1;
		errno = ENOMEM;
		goto out;
	}

	/* populate loc */
	GLFS_LOC_FILL_INODE (inode, loc, out);

	/* fop/op */
	ret = syncop_open (subvol, &loc, flags, glfd->fd);
        DECODE_SYNCOP_ERR (ret);

out:
	loc_wipe (&loc);

	if (inode)
		inode_unref (inode);

	if (ret && glfd) {
		glfs_fd_destroy (glfd);
		glfd = NULL;
	} else {
		glfd->fd->flags = flags;
		fd_bind (glfd->fd);
		glfs_fd_bind (glfd);
	}

	glfs_subvol_done (fs, subvol);

	return glfd;
}

struct glfs_object *
glfs_h_creat (struct glfs *fs, struct glfs_object *parent, const char *path,
	      int flags, mode_t mode, struct stat *stat)
{
	int                 ret = -1;
	struct glfs_fd     *glfd = NULL;
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

	__glfs_entry_fs (fs);

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

	uuid_generate (gfid);
	ret = dict_set_static_bin (xattr_req, "gfid-req", gfid, 16);
	if (ret) {
		ret = -1;
		errno = ENOMEM;
		goto out;
	}

	GLFS_LOC_FILL_PINODE (inode, loc, ret, errno, out, path);

	glfd = glfs_fd_new (fs);
	if (!glfd)
		goto out;

	glfd->fd = fd_create (loc.inode, getpid());
	if (!glfd->fd) {
		ret = -1;
		errno = ENOMEM;
		goto out;
	}

	/* fop/op */
	ret = syncop_create (subvol, &loc, flags, mode, glfd->fd,
			     xattr_req, &iatt);
        DECODE_SYNCOP_ERR (ret);

	/* populate out args */
	if (ret == 0) {
		/* TODO: If the inode existed in the cache (say file already
		   exists), then the glfs_loc_link will not update the
		   loc.inode, as a result we will have a 0000 GFID that we
		   would copy out to the object, this needs to be fixed.
		*/
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

	if (glfd) {
		glfs_fd_destroy (glfd);
		glfd = NULL;
	}

	glfs_subvol_done (fs, subvol);

	return object;
}

struct glfs_object *
glfs_h_mkdir (struct glfs *fs, struct glfs_object *parent, const char *path,
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

	__glfs_entry_fs (fs);

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

	uuid_generate (gfid);
	ret = dict_set_static_bin (xattr_req, "gfid-req", gfid, 16);
	if (ret) {
		ret = -1;
		errno = ENOMEM;
		goto out;
	}

	GLFS_LOC_FILL_PINODE (inode, loc, ret, errno, out, path);

	/* fop/op */
	ret = syncop_mkdir (subvol, &loc, mode, xattr_req, &iatt);
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

	return object;
}

struct glfs_object *
glfs_h_mknod (struct glfs *fs, struct glfs_object *parent, const char *path,
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

	__glfs_entry_fs (fs);

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

	uuid_generate (gfid);
	ret = dict_set_static_bin (xattr_req, "gfid-req", gfid, 16);
	if (ret) {
		ret = -1;
		errno = ENOMEM;
		goto out;
	}

	GLFS_LOC_FILL_PINODE (inode, loc, ret, errno, out, path);

	/* fop/op */
	ret = syncop_mknod (subvol, &loc, mode, dev, xattr_req, &iatt);
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

	return object;
}

int
glfs_h_unlink (struct glfs *fs, struct glfs_object *parent, const char *path)
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

	__glfs_entry_fs (fs);

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
		ret = syncop_unlink (subvol, &loc);
                DECODE_SYNCOP_ERR (ret);
		if (ret != 0) {
			goto out;
		}
	} else {
		ret = syncop_rmdir (subvol, &loc, 0);
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

	return ret;
}

struct glfs_fd *
glfs_h_opendir (struct glfs *fs, struct glfs_object *object)
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

	__glfs_entry_fs (fs);

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
	ret = syncop_opendir (subvol, &loc, glfd->fd);
        DECODE_SYNCOP_ERR (ret);

out:
	loc_wipe (&loc);

	if (inode)
		inode_unref (inode);

	if (ret && glfd) {
		glfs_fd_destroy (glfd);
		glfd = NULL;
	} else {
		fd_bind (glfd->fd);
		glfs_fd_bind (glfd);
	}

	glfs_subvol_done (fs, subvol);

	return glfd;
}

ssize_t
glfs_h_extract_handle (struct glfs_object *object, unsigned char *handle,
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

struct glfs_object *
glfs_h_create_from_handle (struct glfs *fs, unsigned char *handle, int len,
			   struct stat *stat)
{
	loc_t               loc = {0, };
	int                 ret = -1;
	struct iatt         iatt = {0, };
	inode_t            *newinode = NULL;
	xlator_t           *subvol = NULL;
	struct glfs_object *object = NULL;

	/* validate in args */
	if ((fs == NULL) || (handle == NULL) || (len != GFAPI_HANDLE_LENGTH)) {
		errno = EINVAL;
		return NULL;
	}

	__glfs_entry_fs (fs);

	/* get the active volume */
	subvol = glfs_active_subvol (fs);
	if (!subvol) {
		errno = EIO;
		goto out;
	}

	memcpy (loc.gfid, handle, GFAPI_HANDLE_LENGTH);

	newinode = inode_find (subvol->itable, loc.gfid);
	if (newinode)
		loc.inode = newinode;
	else {
		loc.inode = inode_new (subvol->itable);
		if (!loc.inode) {
			errno = ENOMEM;
			goto out;
		}
	}

	ret = syncop_lookup (subvol, &loc, 0, &iatt, 0, 0);
        DECODE_SYNCOP_ERR (ret);
	if (ret) {
		gf_log (subvol->name, GF_LOG_WARNING,
			"inode refresh of %s failed: %s",
			uuid_utoa (loc.gfid), strerror (errno));
		goto out;
	}

	newinode = inode_link (loc.inode, 0, 0, &iatt);
	if (newinode)
		inode_lookup (newinode);
	else {
		gf_log (subvol->name, GF_LOG_WARNING,
			"inode linking of %s failed: %s",
			uuid_utoa (loc.gfid), strerror (errno));
		errno = EINVAL;
		goto out;
	}

	/* populate stat */
	if (stat)
		glfs_iatt_to_stat (fs, &iatt, stat);

	object = GF_CALLOC (1, sizeof(struct glfs_object),
			    glfs_mt_glfs_object_t);
	if (object == NULL) {
		errno = ENOMEM;
		ret = -1;
		goto out;
	}

	/* populate the return object */
	object->inode = newinode;
	uuid_copy (object->gfid, object->inode->gfid);

out:
	/* TODO: Check where the inode ref is being held? */
	loc_wipe (&loc);

	glfs_subvol_done (fs, subvol);

	return object;
}

int
glfs_h_close (struct glfs_object *object)
{
	/* Release the held reference */
	inode_unref (object->inode);
	GF_FREE (object);

	return 0;
}

int
glfs_h_truncate (struct glfs *fs, struct glfs_object *object, off_t offset)
{
	loc_t               loc = {0, };
	int                 ret = -1;
	xlator_t           *subvol = NULL;
	inode_t            *inode = NULL;

	/* validate in args */
	if ((fs == NULL) || (object == NULL)) {
		errno = EINVAL;
		return -1;
	}

	__glfs_entry_fs (fs);

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
	ret = syncop_truncate (subvol, &loc, (off_t)offset);
        DECODE_SYNCOP_ERR (ret);

	/* populate out args */
	if (ret == 0)
		ret = glfs_loc_unlink (&loc);

out:
	loc_wipe (&loc);

	if (inode)
		inode_unref (inode);

	glfs_subvol_done (fs, subvol);

	return ret;
}

struct glfs_object *
glfs_h_symlink (struct glfs *fs, struct glfs_object *parent, const char *name,
		const char *data, struct stat *stat)
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
	if ((fs == NULL) || (parent == NULL) || (name == NULL) ||
		(data == NULL)) {
		errno = EINVAL;
		return NULL;
	}

	__glfs_entry_fs (fs);

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

	uuid_generate (gfid);
	ret = dict_set_static_bin (xattr_req, "gfid-req", gfid, 16);
	if (ret) {
		ret = -1;
		errno = ENOMEM;
		goto out;
	}

	GLFS_LOC_FILL_PINODE (inode, loc, ret, errno, out, name);

	/* fop/op */
	ret = syncop_symlink (subvol, &loc, data, xattr_req, &iatt);
        DECODE_SYNCOP_ERR (ret);

	/* populate out args */
	if (ret == 0) {
		/* TODO: If the inode existed in the cache (say file already
		 * exists), then the glfs_loc_link will not update the
		 * loc.inode, as a result we will have a 0000 GFID that we
		 * would copy out to the object, this needs to be fixed.
		 */
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

	return object;
}

int
glfs_h_readlink (struct glfs *fs, struct glfs_object *object, char *buf,
		 size_t bufsiz)
{
	loc_t               loc = {0, };
	int                 ret = -1;
	xlator_t           *subvol = NULL;
	inode_t            *inode = NULL;
	char               *linkval = NULL;

	/* validate in args */
	if ((fs == NULL) || (object == NULL) || (buf == NULL)) {
		errno = EINVAL;
		return -1;
	}

	__glfs_entry_fs (fs);

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
	ret = syncop_readlink (subvol, &loc, &linkval, bufsiz);
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

	return ret;
}

int
glfs_h_link (struct glfs *fs, struct glfs_object *linksrc,
	     struct glfs_object *parent, const char *name)
{
	int                 ret = -1;
	xlator_t           *subvol = NULL;
	inode_t            *inode = NULL;
	inode_t            *pinode = NULL;
	loc_t               oldloc = {0, };
	loc_t               newloc = {0, };

	/* validate in args */
	if ((fs == NULL) || (linksrc == NULL) || (parent == NULL) ||
		(name == NULL)) {
		errno = EINVAL;
		return -1;
	}

	__glfs_entry_fs (fs);

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
	ret = syncop_link (subvol, &oldloc, &newloc);
        DECODE_SYNCOP_ERR (ret);

	if (ret == 0)
		/* TODO: No iatt to pass as there has been no lookup */
		ret = glfs_loc_link (&newloc, NULL);
out:
	loc_wipe (&oldloc);
	loc_wipe (&newloc);

	if (inode)
		inode_unref (inode);

	if (pinode)
		inode_unref (pinode);

	glfs_subvol_done (fs, subvol);

	return ret;
}

int
glfs_h_rename (struct glfs *fs, struct glfs_object *olddir, const char *oldname,
	       struct glfs_object *newdir, const char *newname)
{
	int                 ret = -1;
	xlator_t           *subvol = NULL;
	inode_t            *oldpinode = NULL;
	inode_t            *newpinode = NULL;
	loc_t               oldloc = {0, };
	loc_t               newloc = {0, };
	struct iatt         oldiatt = {0, };
	struct iatt         newiatt = {0, };

	/* validate in args */
	if ((fs == NULL) || (olddir == NULL) || (oldname == NULL) ||
		(newdir == NULL) || (newname == NULL)) {
		errno = EINVAL;
		return -1;
	}

	__glfs_entry_fs (fs);

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
			errno = EISDIR;
			goto out;
		}
	}

	/* TODO: check if new or old is a prefix of the other, and fail EINVAL */

	ret = syncop_rename (subvol, &oldloc, &newloc);
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

	return ret;
}
