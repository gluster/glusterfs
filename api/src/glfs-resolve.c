/*
  Copyright (c) 2012 Red Hat, Inc. <http://www.redhat.com>
  This file is part of GlusterFS.

  This file is licensed to you under your choice of the GNU Lesser
  General Public License, version 3 or any later version (LGPLv3 or
  later), or the GNU General Public License, version 2 (GPLv2), in all
  cases as published by the Free Software Foundation.
*/


#include <unistd.h>
#include <string.h>
#include <stdlib.h>
#include <stdio.h>
#include <inttypes.h>
#include <limits.h>

#ifndef _CONFIG_H
#define _CONFIG_H
#include "config.h"
#endif

#include "glusterfs.h"
#include "logging.h"
#include "stack.h"
#include "event.h"
#include "glfs-mem-types.h"
#include "common-utils.h"
#include "syncop.h"
#include "call-stub.h"

#include "glfs-internal.h"

#define graphid_str(subvol) (uuid_utoa((unsigned char *)subvol->graph->graph_uuid))


int
glfs_first_lookup_safe (xlator_t *subvol)
{
	loc_t  loc = {0, };
	int    ret = -1;

	loc.inode = subvol->itable->root;
	memset (loc.gfid, 0, 16);
	loc.gfid[15] = 1;
	loc.path = "/";
	loc.name = "";

	ret = syncop_lookup (subvol, &loc, 0, 0, 0, 0);
        DECODE_SYNCOP_ERR (ret);

	gf_log (subvol->name, GF_LOG_DEBUG, "first lookup complete %d", ret);

	return ret;
}


int
__glfs_first_lookup (struct glfs *fs, xlator_t *subvol)
{
	int ret = -1;

	fs->migration_in_progress = 1;
	pthread_mutex_unlock (&fs->mutex);
	{
		ret = glfs_first_lookup_safe (subvol);
	}
	pthread_mutex_lock (&fs->mutex);
	fs->migration_in_progress = 0;
	pthread_cond_broadcast (&fs->cond);

	return ret;
}


inode_t *
glfs_refresh_inode_safe (xlator_t *subvol, inode_t *oldinode)
{
	loc_t        loc = {0, };
	int          ret = -1;
	struct iatt  iatt = {0, };
	inode_t     *newinode = NULL;


	if (!oldinode)
		return NULL;

	if (oldinode->table->xl == subvol)
		return inode_ref (oldinode);

	newinode = inode_find (subvol->itable, oldinode->gfid);
	if (newinode)
		return newinode;

	uuid_copy (loc.gfid, oldinode->gfid);
	loc.inode = inode_new (subvol->itable);
	if (!loc.inode)
		return NULL;

	ret = syncop_lookup (subvol, &loc, 0, &iatt, 0, 0);
        DECODE_SYNCOP_ERR (ret);

	if (ret) {
		gf_log (subvol->name, GF_LOG_WARNING,
			"inode refresh of %s failed: %s",
			uuid_utoa (oldinode->gfid), strerror (errno));
		loc_wipe (&loc);
		return NULL;
	}

	newinode = inode_link (loc.inode, 0, 0, &iatt);
	if (newinode)
		inode_lookup (newinode);

	loc_wipe (&loc);

	return newinode;
}


inode_t *
__glfs_refresh_inode (struct glfs *fs, xlator_t *subvol, inode_t *inode)
{
	inode_t *newinode = NULL;

	fs->migration_in_progress = 1;
	pthread_mutex_unlock (&fs->mutex);
	{
		newinode = glfs_refresh_inode_safe (subvol, inode);
	}
	pthread_mutex_lock (&fs->mutex);
	fs->migration_in_progress = 0;
	pthread_cond_broadcast (&fs->cond);

	return newinode;
}

int
glfs_loc_touchup (loc_t *loc)
{
	char *path = NULL;
	int   ret = -1;
	char *bn = NULL;

	if (loc->parent)
		ret = inode_path (loc->parent, loc->name, &path);
	else
		ret = inode_path (loc->inode, 0, &path);

	loc->path = path;

	if (ret < 0 || !path) {
		ret = -1;
		errno = ENOMEM;
		goto out;
	}

	bn = strrchr (path, '/');
	if (bn)
		bn++;
	loc->name = bn;
	ret = 0;
out:
	return ret;
}


int
glfs_resolve_symlink (struct glfs *fs, xlator_t *subvol, inode_t *inode,
		      char **lpath)
{
	loc_t  loc = {0, };
	char  *path = NULL;
	char  *rpath = NULL;
	int    ret = -1;

	loc.inode = inode_ref (inode);
	uuid_copy (loc.gfid, inode->gfid);
	ret = inode_path (inode, NULL, &rpath);
	if (ret < 0)
		goto out;
	loc.path = rpath;

	ret = syncop_readlink (subvol, &loc, &path, 4096);
        DECODE_SYNCOP_ERR (ret);

	if (ret < 0)
		goto out;

	if (lpath)
		*lpath = path;
out:
	loc_wipe (&loc);
	return ret;
}


int
glfs_resolve_base (struct glfs *fs, xlator_t *subvol, inode_t *inode,
		   struct iatt *iatt)
{
	loc_t       loc = {0, };
	int         ret = -1;
	char       *path = NULL;

	loc.inode = inode_ref (inode);
	uuid_copy (loc.gfid, inode->gfid);

	ret = inode_path (loc.inode, NULL, &path);
	loc.path = path;
	if (ret < 0)
		goto out;

	ret = syncop_lookup (subvol, &loc, NULL, iatt, NULL, NULL);
        DECODE_SYNCOP_ERR (ret);
out:
	loc_wipe (&loc);

	return ret;
}


inode_t *
glfs_resolve_component (struct glfs *fs, xlator_t *subvol, inode_t *parent,
			const char *component, struct iatt *iatt,
			int force_lookup)
{
	loc_t        loc = {0, };
	inode_t     *inode = NULL;
	int          reval = 0;
	int          ret = -1;
	int          glret = -1;
	struct iatt  ciatt = {0, };
	uuid_t       gfid;
	dict_t      *xattr_req = NULL;

	loc.name = component;

	loc.parent = inode_ref (parent);
	uuid_copy (loc.pargfid, parent->gfid);


	if (strcmp (component, ".") == 0)
		loc.inode = inode_ref (parent);
	else if (strcmp (component, "..") == 0)
		loc.inode = inode_parent (parent, 0, 0);
	else
		loc.inode = inode_grep (parent->table, parent, component);

	if (loc.inode) {
		uuid_copy (loc.gfid, loc.inode->gfid);
		reval = 1;

		if (!force_lookup) {
			inode = inode_ref (loc.inode);
			ciatt.ia_type = inode->ia_type;
			goto found;
		}
	} else {
		uuid_generate (gfid);
		loc.inode = inode_new (parent->table);
	}

	if (!loc.inode)
		goto out;

	glret = glfs_loc_touchup (&loc);
	if (glret < 0) {
		ret = -1;
		goto out;
	}

	ret = syncop_lookup (subvol, &loc, NULL, &ciatt, NULL, NULL);
        DECODE_SYNCOP_ERR (ret);
	if (ret && reval) {
		inode_unref (loc.inode);
		loc.inode = inode_new (parent->table);
		if (!loc.inode) {
			errno = ENOMEM;
			goto out;
		}

		xattr_req = dict_new ();
		if (!xattr_req) {
			errno = ENOMEM;
			goto out;
		}

		uuid_generate (gfid);

		ret = dict_set_static_bin (xattr_req, "gfid-req", gfid, 16);
		if (ret) {
			errno = ENOMEM;
			goto out;
		}

		ret = syncop_lookup (subvol, &loc, xattr_req, &ciatt,
				     NULL, NULL);
                DECODE_SYNCOP_ERR (ret);
	}
	if (ret)
		goto out;

	inode = inode_link (loc.inode, loc.parent, component, &ciatt);
found:
	if (inode)
		inode_lookup (inode);
	if (iatt)
		*iatt = ciatt;
out:
	if (xattr_req)
		dict_unref (xattr_req);

	loc_wipe (&loc);

	return inode;
}


int
glfs_resolve_at (struct glfs *fs, xlator_t *subvol, inode_t *at,
		 const char *origpath, loc_t *loc, struct iatt *iatt,
		 int follow, int reval)
{
	inode_t    *inode = NULL;
	inode_t    *parent = NULL;
	char       *saveptr = NULL;
	char       *path = NULL;
	char       *component = NULL;
	char       *next_component = NULL;
	int         ret = -1;
	struct iatt ciatt = {0, };

	path = gf_strdup (origpath);
	if (!path) {
		errno = ENOMEM;
		return -1;
	}

	parent = NULL;
	if (at && path[0] != '/') {
		/* A relative resolution of a path which starts with '/'
		   is equal to an absolute path resolution.
		*/
		inode = inode_ref (at);
	} else {
		inode = inode_ref (subvol->itable->root);

		if (strcmp (path, "/") == 0)
			glfs_resolve_base (fs, subvol, inode, &ciatt);
	}

	for (component = strtok_r (path, "/", &saveptr);
	     component; component = next_component) {

		next_component = strtok_r (NULL, "/", &saveptr);

		if (parent)
			inode_unref (parent);

		parent = inode;

		inode = glfs_resolve_component (fs, subvol, parent,
						component, &ciatt,
						/* force hard lookup on the last
						   component, as the caller
						   wants proper iatt filled
						*/
						(reval || (!next_component &&
						iatt)));
		if (!inode)
			break;

		if (IA_ISLNK (ciatt.ia_type) && (next_component || follow)) {
			/* If the component is not the last piece,
			   then following it is necessary even if
			   not requested by the caller
			*/
			char *lpath = NULL;
			loc_t sym_loc = {0,};

			if (follow > GLFS_SYMLINK_MAX_FOLLOW) {
				errno = ELOOP;
				ret = -1;
				if (inode) {
					inode_unref (inode);
					inode = NULL;
				}
				break;
			}

			ret = glfs_resolve_symlink (fs, subvol, inode, &lpath);
			inode_unref (inode);
			inode = NULL;
			if (ret < 0)
				break;

			ret = glfs_resolve_at (fs, subvol, parent, lpath,
					       &sym_loc,
					       /* followed iatt becomes the
						  component iatt
					       */
					       &ciatt,
					       /* always recurisvely follow while
						  following symlink
					       */
					       follow + 1, reval);
			if (ret == 0)
				inode = inode_ref (sym_loc.inode);
			loc_wipe (&sym_loc);
			GF_FREE (lpath);
		}

		if (!next_component)
			break;

		if (!IA_ISDIR (ciatt.ia_type)) {
			/* next_component exists and this component is
			   not a directory
			*/
			inode_unref (inode);
			inode = NULL;
			ret = -1;
			errno = ENOTDIR;
			break;
		}
	}

	if (parent && next_component)
		/* resolution failed mid-way */
		goto out;

	/* At this point, all components up to the last parent directory
	   have been resolved successfully (@parent). Resolution of basename
	   might have failed (@inode) if at all.
	*/

	loc->parent = parent;
	if (parent) {
		uuid_copy (loc->pargfid, parent->gfid);
		loc->name = component;
	}

	loc->inode = inode;
	if (inode) {
		uuid_copy (loc->gfid, inode->gfid);
		if (iatt)
			*iatt = ciatt;
		ret = 0;
	}

	glfs_loc_touchup (loc);
out:
	GF_FREE (path);

	/* do NOT loc_wipe here as only last component might be missing */

	return ret;
}


int
glfs_resolve_path (struct glfs *fs, xlator_t *subvol, const char *origpath,
		   loc_t *loc, struct iatt *iatt, int follow, int reval)
{
	int ret = -1;
	inode_t *cwd = NULL;

	if (origpath[0] == '/')
		return glfs_resolve_at (fs, subvol, NULL, origpath, loc, iatt,
					follow, reval);

	cwd = glfs_cwd_get (fs);

	ret = glfs_resolve_at (fs, subvol, cwd, origpath, loc, iatt,
			       follow, reval);
	if (cwd)
		inode_unref (cwd);

	return ret;
}


int
glfs_resolve (struct glfs *fs, xlator_t *subvol, const char *origpath,
	      loc_t *loc, struct iatt *iatt, int reval)
{
	int ret = -1;

	ret = glfs_resolve_path (fs, subvol, origpath, loc, iatt, 1, reval);

	return ret;
}


int
glfs_lresolve (struct glfs *fs, xlator_t *subvol, const char *origpath,
	       loc_t *loc, struct iatt *iatt, int reval)
{
	int ret = -1;

	ret = glfs_resolve_path (fs, subvol, origpath, loc, iatt, 0, reval);

	return ret;
}


int
glfs_migrate_fd_locks_safe (struct glfs *fs, xlator_t *oldsubvol, fd_t *oldfd,
			    xlator_t *newsubvol, fd_t *newfd)
{
	dict_t *lockinfo = NULL;
	int ret = 0;
	char uuid1[64];

	if (!oldfd->lk_ctx || fd_lk_ctx_empty (oldfd->lk_ctx))
		return 0;

	newfd->lk_ctx = fd_lk_ctx_ref (oldfd->lk_ctx);

	ret = syncop_fgetxattr (oldsubvol, oldfd, &lockinfo,
				GF_XATTR_LOCKINFO_KEY);
        DECODE_SYNCOP_ERR (ret);
	if (ret < 0) {
		gf_log (fs->volname, GF_LOG_WARNING,
			"fgetxattr (%s) failed (%s) on graph %s (%d)",
			uuid_utoa_r (oldfd->inode->gfid, uuid1),
			strerror (errno),
			graphid_str (oldsubvol), oldsubvol->graph->id);
		goto out;
	}

	if (!dict_get (lockinfo, GF_XATTR_LOCKINFO_KEY)) {
		gf_log (fs->volname, GF_LOG_WARNING,
			"missing lokinfo key (%s) on graph %s (%d)",
			uuid_utoa_r (oldfd->inode->gfid, uuid1),
			graphid_str (oldsubvol), oldsubvol->graph->id);
		goto out;
	}

	ret = syncop_fsetxattr (newsubvol, newfd, lockinfo, 0);
        DECODE_SYNCOP_ERR (ret);
	if (ret < 0) {
		gf_log (fs->volname, GF_LOG_WARNING,
			"fsetxattr (%s) failed (%s) on graph %s (%d)",
			uuid_utoa_r (newfd->inode->gfid, uuid1),
			strerror (errno),
			graphid_str (newsubvol), newsubvol->graph->id);
		goto out;
	}
out:
	if (lockinfo)
		dict_unref (lockinfo);
	return ret;
}


fd_t *
glfs_migrate_fd_safe (struct glfs *fs, xlator_t *newsubvol, fd_t *oldfd)
{
	fd_t *newfd = NULL;
	inode_t *oldinode = NULL;
	inode_t *newinode = NULL;
	xlator_t *oldsubvol = NULL;
	int ret = -1;
	loc_t loc = {0, };
	char uuid1[64];


	oldinode = oldfd->inode;
	oldsubvol = oldinode->table->xl;

	if (oldsubvol == newsubvol)
		return fd_ref (oldfd);

	if (!oldsubvol->switched) {
		ret = syncop_fsync (oldsubvol, oldfd, 0);
                DECODE_SYNCOP_ERR (ret);
		if (ret) {
			gf_log (fs->volname, GF_LOG_WARNING,
				"fsync() failed (%s) on %s graph %s (%d)",
				strerror (errno),
				uuid_utoa_r (oldfd->inode->gfid, uuid1),
				graphid_str (oldsubvol), oldsubvol->graph->id);
		}
	}

	newinode = glfs_refresh_inode_safe (newsubvol, oldinode);
	if (!newinode) {
		gf_log (fs->volname, GF_LOG_WARNING,
			"inode (%s) refresh failed (%s) on graph %s (%d)",
			uuid_utoa_r (oldinode->gfid, uuid1),
			strerror (errno),
			graphid_str (newsubvol), newsubvol->graph->id);
		goto out;
	}

	newfd = fd_create (newinode, getpid());
	if (!newfd) {
		gf_log (fs->volname, GF_LOG_WARNING,
			"fd_create (%s) failed (%s) on graph %s (%d)",
			uuid_utoa_r (newinode->gfid, uuid1),
			strerror (errno),
			graphid_str (newsubvol), newsubvol->graph->id);
		goto out;
	}

	loc.inode = inode_ref (newinode);

        ret = inode_path (oldfd->inode, NULL, (char **)&loc.path);
        if (ret < 0) {
                gf_log (fs->volname, GF_LOG_INFO, "inode_path failed");
                goto out;
        }

        uuid_copy (loc.gfid, oldinode->gfid);


	if (IA_ISDIR (oldinode->ia_type))
		ret = syncop_opendir (newsubvol, &loc, newfd);
	else
		ret = syncop_open (newsubvol, &loc,
				   oldfd->flags & ~(O_TRUNC|O_EXCL|O_CREAT),
				   newfd);
        DECODE_SYNCOP_ERR (ret);
	loc_wipe (&loc);

	if (ret) {
		gf_log (fs->volname, GF_LOG_WARNING,
			"syncop_open%s (%s) failed (%s) on graph %s (%d)",
			IA_ISDIR (oldinode->ia_type) ? "dir" : "",
			uuid_utoa_r (newinode->gfid, uuid1),
			strerror (errno),
			graphid_str (newsubvol), newsubvol->graph->id);
		goto out;
	}

	ret = glfs_migrate_fd_locks_safe (fs, oldsubvol, oldfd, newsubvol,
					  newfd);

	if (ret) {
		gf_log (fs->volname, GF_LOG_WARNING,
			"lock migration (%s) failed (%s) on graph %s (%d)",
			uuid_utoa_r (newinode->gfid, uuid1),
			strerror (errno),
			graphid_str (newsubvol), newsubvol->graph->id);
		goto out;
	}

        newfd->flags = oldfd->flags;
	fd_bind (newfd);
out:
	if (newinode)
		inode_unref (newinode);

	if (ret) {
		fd_unref (newfd);
		newfd = NULL;
	}

	return newfd;
}


fd_t *
__glfs_migrate_fd (struct glfs *fs, xlator_t *newsubvol, struct glfs_fd *glfd)
{
	fd_t *oldfd = NULL;
	fd_t *newfd = NULL;

	oldfd = glfd->fd;

	fs->migration_in_progress = 1;
	pthread_mutex_unlock (&fs->mutex);
	{
		newfd = glfs_migrate_fd_safe (fs, newsubvol, oldfd);
	}
	pthread_mutex_lock (&fs->mutex);
	fs->migration_in_progress = 0;
	pthread_cond_broadcast (&fs->cond);

	return newfd;
}


fd_t *
__glfs_resolve_fd (struct glfs *fs, xlator_t *subvol, struct glfs_fd *glfd)
{
	fd_t *fd = NULL;

	if (glfd->fd->inode->table->xl == subvol)
		return fd_ref (glfd->fd);

	fd = __glfs_migrate_fd (fs, subvol, glfd);
	if (!fd)
		return NULL;

	if (subvol == fs->active_subvol) {
		fd_unref (glfd->fd);
		glfd->fd = fd_ref (fd);
	}

	return fd;
}


fd_t *
glfs_resolve_fd (struct glfs *fs, xlator_t *subvol, struct glfs_fd *glfd)
{
	fd_t *fd = NULL;

	glfs_lock (fs);
	{
		fd = __glfs_resolve_fd (fs, subvol, glfd);
	}
	glfs_unlock (fs);

	return fd;
}


void
__glfs_migrate_openfds (struct glfs *fs, xlator_t *subvol)
{
	struct glfs_fd *glfd = NULL;
	fd_t *fd = NULL;

	list_for_each_entry (glfd, &fs->openfds, openfds) {
		if (uuid_is_null (glfd->fd->inode->gfid)) {
			gf_log (fs->volname, GF_LOG_INFO,
				"skipping openfd %p/%p in graph %s (%d)",
				glfd, glfd->fd,	graphid_str(subvol),
				subvol->graph->id);
			/* create in progress, defer */
			continue;
		}

		fd = __glfs_migrate_fd (fs, subvol, glfd);
		if (fd) {
			fd_unref (glfd->fd);
			glfd->fd = fd;
		}
	}
}


xlator_t *
__glfs_active_subvol (struct glfs *fs)
{
	xlator_t      *new_subvol = NULL;
	int            ret = -1;
	inode_t       *new_cwd = NULL;

	if (!fs->next_subvol)
		return fs->active_subvol;

	new_subvol = fs->next_subvol;

	ret = __glfs_first_lookup (fs, new_subvol);
	if (ret) {
		gf_log (fs->volname, GF_LOG_INFO,
			"first lookup on graph %s (%d) failed (%s)",
			graphid_str (new_subvol), new_subvol->graph->id,
			strerror (errno));
		return NULL;
	}

	if (fs->cwd) {
		new_cwd = __glfs_refresh_inode (fs, new_subvol, fs->cwd);

		if (!new_cwd) {
			char buf1[64];
			gf_log (fs->volname, GF_LOG_INFO,
				"cwd refresh of %s graph %s (%d) failed (%s)",
				uuid_utoa_r (fs->cwd->gfid, buf1),
				graphid_str (new_subvol),
				new_subvol->graph->id, strerror (errno));
			return NULL;
		}
	}

	__glfs_migrate_openfds (fs, new_subvol);

	/* switching @active_subvol and @cwd
	   should be atomic
	*/
	fs->old_subvol = fs->active_subvol;
	fs->active_subvol = fs->next_subvol;
	fs->next_subvol = NULL;

	if (new_cwd) {
		__glfs_cwd_set (fs, new_cwd);
		inode_unref (new_cwd);
	}

	gf_log (fs->volname, GF_LOG_INFO, "switched to graph %s (%d)",
		graphid_str (new_subvol), new_subvol->graph->id);

	return new_subvol;
}

xlator_t *
glfs_active_subvol (struct glfs *fs)
{
	xlator_t      *subvol = NULL;
	xlator_t      *old_subvol = NULL;

	glfs_lock (fs);
	{
		subvol = __glfs_active_subvol (fs);

		if (subvol)
			subvol->winds++;

		if (fs->old_subvol) {
			old_subvol = fs->old_subvol;
			fs->old_subvol = NULL;
			old_subvol->switched = 1;
		}
	}
	glfs_unlock (fs);

	if (old_subvol)
		glfs_subvol_done (fs, old_subvol);

	return subvol;
}


void
glfs_subvol_done (struct glfs *fs, xlator_t *subvol)
{
	int ref = 0;
	xlator_t *active_subvol = NULL;

	if (!subvol)
		return;

	glfs_lock (fs);
	{
		ref = (--subvol->winds);
		active_subvol = fs->active_subvol;
	}
	glfs_unlock (fs);

	if (ref == 0) {
		assert (subvol != active_subvol);
		xlator_notify (subvol, GF_EVENT_PARENT_DOWN, subvol, NULL);
	}
}


int
__glfs_cwd_set (struct glfs *fs, inode_t *inode)
{
	if (inode->table->xl != fs->active_subvol) {
		inode = __glfs_refresh_inode (fs, fs->active_subvol, inode);
		if (!inode)
			return -1;
	} else {
		inode_ref (inode);
	}

	if (fs->cwd)
		inode_unref (fs->cwd);

	fs->cwd = inode;

	return 0;
}


int
glfs_cwd_set (struct glfs *fs, inode_t *inode)
{
	int ret = 0;

	glfs_lock (fs);
	{
		ret = __glfs_cwd_set (fs, inode);
	}
	glfs_unlock (fs);

	return ret;
}


inode_t *
__glfs_cwd_get (struct glfs *fs)
{
	inode_t *cwd = NULL;

	if (!fs->cwd)
		return NULL;

	if (fs->cwd->table->xl == fs->active_subvol) {
		cwd = inode_ref (fs->cwd);
		return cwd;
	}

	cwd = __glfs_refresh_inode (fs, fs->active_subvol, fs->cwd);

	return cwd;
}

inode_t *
glfs_cwd_get (struct glfs *fs)
{
	inode_t *cwd = NULL;

	glfs_lock (fs);
	{
		cwd = __glfs_cwd_get (fs);
	}
	glfs_unlock (fs);

	return cwd;
}

inode_t *
__glfs_resolve_inode (struct glfs *fs, xlator_t *subvol,
		    struct glfs_object *object)
{
	inode_t *inode = NULL;

	if (object->inode->table->xl == subvol)
		return inode_ref (object->inode);

	inode = __glfs_refresh_inode (fs, fs->active_subvol,
					object->inode);
	if (!inode)
		return NULL;

	if (subvol == fs->active_subvol) {
		inode_unref (object->inode);
		object->inode = inode_ref (inode);
	}

	return inode;
}

inode_t *
glfs_resolve_inode (struct glfs *fs, xlator_t *subvol,
		    struct glfs_object *object)
{
	inode_t *inode = NULL;

	glfs_lock (fs);
	{
		inode = __glfs_resolve_inode(fs, subvol, object);
	}
	glfs_unlock (fs);

	return inode;
}

int
glfs_create_object (loc_t *loc, struct glfs_object **retobject)
{
	struct glfs_object *object = NULL;

	object = GF_CALLOC (1, sizeof(struct glfs_object),
			    glfs_mt_glfs_object_t);
	if (object == NULL) {
		errno = ENOMEM;
		return -1;
	}

	object->inode = loc->inode;
	uuid_copy (object->gfid, object->inode->gfid);

	/* we hold the reference */
	loc->inode = NULL;

	*retobject = object;

	return 0;
}
