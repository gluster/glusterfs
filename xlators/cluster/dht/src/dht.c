/*
   Copyright (c) 2008 Z RESEARCH, Inc. <http://www.zresearch.com>
   This file is part of GlusterFS.

   GlusterFS is free software; you can redistribute it and/or modify
   it under the terms of the GNU General Public License as published
   by the Free Software Foundation; either version 3 of the License,
   or (at your option) any later version.

   GlusterFS is distributed in the hope that it will be useful, but
   WITHOUT ANY WARRANTY; without even the implied warranty of
   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
   General Public License for more details.

   You should have received a copy of the GNU General Public License
   along with this program.  If not, see
   <http://www.gnu.org/licenses/>.
*/


#ifndef _CONFIG_H
#define _CONFIG_H
#include "config.h"
#endif

/* TODO: add NS locking */

#include "glusterfs.h"
#include "xlator.h"
#include "dht.h"
#include "defaults.h"


/* TODO:
   - use volumename in xattr instead of "dht"
   - use NS locks
   - handle all cases in self heal layout reconstruction
   - complete linkfile selfheal
*/


int
dht_lookup_selfheal_cbk (call_frame_t *frame, void *cookie,
			 xlator_t *this,
			 int op_ret, int op_errno)
{
	dht_local_t  *local = NULL;
	dht_layout_t *layout = NULL;
	int           ret = 0;

	local = frame->local;
	ret = op_ret;

	if (ret == 0) {
		layout = local->selfheal.layout;
		ret = inode_ctx_set (local->inode, this, layout);

		if (ret == 0)
			local->selfheal.layout = NULL;
	}

	DHT_STACK_UNWIND (frame, ret, local->op_errno, local->inode,
			  &local->stbuf, local->xattr);

	return 0;
}


int
dht_lookup_dir_cbk (call_frame_t *frame, void *cookie, xlator_t *this,
                    int op_ret, int op_errno,
                    inode_t *inode, struct stat *stbuf, dict_t *xattr)
{
        dht_local_t  *local         = NULL;
        int           this_call_cnt = 0;
        call_frame_t *prev          = NULL;
	dht_layout_t *layout        = NULL;
	int           ret           = 0;


        local = frame->local;
        prev  = cookie;
	layout = local->layout;

        LOCK (&frame->lock);
        {
                /* TODO: assert equal mode on stbuf->st_mode and
		   local->stbuf->st_mode

		   else mkdir/chmod/chown and fix
		*/
		/* TODO: assert equal hash type in xattr, local->xattr */

		/* TODO: always ensure same subvolume is in layout->list[0] */

		/* TODO: if subvol is down for hashed lookup, try other
		   subvols in case entry was a directory */

		ret = dht_layout_merge (this, layout, prev->this,
					op_ret, op_errno, xattr);

		if (op_ret == -1) {
			gf_log (this->name, GF_LOG_WARNING,
				"lookup of %s on %s returned error (%s)",
				local->loc.path, prev->this->name,
				strerror (op_errno));

			goto unlock;
		}

		dht_stat_merge (this, &local->stbuf, stbuf, prev->this);
        }
unlock:
        UNLOCK (&frame->lock);


        this_call_cnt = dht_frame_return (frame);

        if (is_last_call (this_call_cnt)) {
		if (local->op_ret == 0) {
			ret = dht_layout_normalize (this, &local->loc, layout);

			local->layout = NULL;

			if (ret != 0) {
				gf_log (this->name, GF_LOG_WARNING,
					"fixing assignment on %s",
					local->loc.path);
				goto selfheal;
			}

			inode_ctx_set (local->inode, this, layout);
		}

		DHT_STACK_UNWIND (frame, local->op_ret, local->op_errno,
				  local->inode, &local->stbuf, local->xattr);
        }

	return 0;

selfheal:
	ret = dht_selfheal_directory (frame, dht_lookup_selfheal_cbk,
				      &local->loc, layout);

	return 0;
}


int
dht_revalidate_cbk (call_frame_t *frame, void *cookie, xlator_t *this,
                    int op_ret, int op_errno,
                    inode_t *inode, struct stat *stbuf, dict_t *xattr)
{
        dht_local_t  *local         = NULL;
        int           this_call_cnt = 0;
        call_frame_t *prev          = NULL;
	dht_layout_t *layout        = NULL;


        local = frame->local;
        prev  = cookie;
	layout = local->layout;

        LOCK (&frame->lock);
        {
		if (op_ret == -1) {
			local->op_errno = op_errno;

			if (op_errno != ENOTCONN && op_errno != ENOENT) {
				gf_log (this->name, GF_LOG_WARNING,
					"subvolume %s returned -1 (%s)",
					prev->this->name, strerror (op_errno));
			}

			goto unlock;
		}

		if (S_IFMT & (stbuf->st_mode ^ local->inode->st_mode)) {
			gf_log (this->name, GF_LOG_WARNING,
				"mismatching filetypes 0%o v/s 0%o for %s",
				(stbuf->st_mode & S_IFMT),
				(local->inode->st_mode & S_IFMT),
				local->loc.path);

			local->op_ret = -1;
			local->op_errno = EINVAL;

			goto unlock;
		}

		dht_stat_merge (this, &local->stbuf, stbuf, prev->this);

		local->op_ret = 0;
		local->stbuf.st_ino = local->st_ino;

		if (!local->xattr)
			local->xattr = dict_ref (xattr);
	}
unlock:
	UNLOCK (&frame->lock);

        this_call_cnt = dht_frame_return (frame);

        if (is_last_call (this_call_cnt)) {
		if (!S_ISDIR (local->stbuf.st_mode)
		    && (local->hashed_subvol != local->cached_subvol))
			local->stbuf.st_mode |= S_ISVTX;
		DHT_STACK_UNWIND (frame, local->op_ret, local->op_errno,
				  local->inode, &local->stbuf, local->xattr);
	}

        return 0;
}


int
dht_lookup_linkfile_create_cbk (call_frame_t *frame, void *cookie,
				xlator_t *this,
				int32_t op_ret, int32_t op_errno,
				inode_t *inode, struct stat *stbuf)
{
	dht_local_t  *local = NULL;
	dht_layout_t *layout = NULL;
	xlator_t     *cached_subvol = NULL;

	local = frame->local;
	cached_subvol = local->cached_subvol;

	layout = dht_layout_for_subvol (this, local->cached_subvol);
	if (!layout) {
		gf_log (this->name, GF_LOG_ERROR,
			"no pre-set layout for subvolume %s",
			cached_subvol ? cached_subvol->name : "<nil>");
		local->op_ret = -1;
		local->op_errno = EINVAL;
		goto unwind;
	}

	inode_ctx_set (local->inode, this, layout);
	local->op_ret = 0;
	local->stbuf.st_mode |= S_ISVTX;

unwind:
	DHT_STACK_UNWIND (frame, local->op_ret, local->op_errno,
			  local->inode, &local->stbuf, local->xattr);
	return 0;
}


int
dht_lookup_everywhere_cbk (call_frame_t *frame, void *cookie, xlator_t *this,
			   int32_t op_ret, int32_t op_errno,
			   inode_t *inode, struct stat *buf, dict_t *xattr)
{
	dht_conf_t   *conf          = NULL;
        dht_local_t  *local         = NULL;
        int           this_call_cnt = 0;
        call_frame_t *prev          = NULL;
	int           is_linkfile   = 0;
	int           is_dir        = 0;
	xlator_t     *subvol        = NULL;
	loc_t        *loc           = NULL;
	xlator_t     *link_subvol   = NULL;
	xlator_t     *hashed_subvol = NULL;
	xlator_t     *cached_subvol = NULL;

	conf   = this->private;

	local  = frame->local;
	loc    = &local->loc;

	prev   = cookie;
	subvol = prev->this;

	LOCK (&frame->lock);
	{
		if (op_ret == -1) {
			if (op_errno != ENOENT)
				local->op_errno = op_errno;
			goto unlock;
		}

		is_linkfile = check_is_linkfile (inode, buf, xattr);
		is_dir = check_is_dir (inode, buf, xattr);

		if (is_linkfile) {
			link_subvol = dht_linkfile_subvol (this, inode, buf,
							   xattr);
			gf_log (this->name, GF_LOG_DEBUG,
				"found on %s linkfile %s (-> %s)",
				subvol->name, loc->path,
				link_subvol ? link_subvol->name : "''");
			goto unlock;
		} else {
			gf_log (this->name, GF_LOG_DEBUG,
				"found on %s file %s",
				subvol->name, loc->path);
		}

		if (!local->cached_subvol) {
			/* found one file */
			dht_stat_merge (this, &local->stbuf, buf, subvol);
			local->xattr = dict_ref (xattr);
			local->cached_subvol = subvol;
		} else {
			gf_log (this->name, GF_LOG_WARNING,
				"multiple subvolumes (%s and %s atleast) have "
				"file %s", local->cached_subvol->name,
				subvol->name, local->loc.path);
		}
	}
unlock:
	UNLOCK (&frame->lock);

	if (is_linkfile) {
		gf_log (this->name, GF_LOG_WARNING,
			"deleting stale linkfile %s on %s",
			loc->path, subvol->name);
		dht_linkfile_unlink (frame, this, subvol, loc);
	}

	this_call_cnt = dht_frame_return (frame);
	if (is_last_call (this_call_cnt)) {
		hashed_subvol = local->hashed_subvol;
		cached_subvol = local->cached_subvol;

		if (!cached_subvol) {
			DHT_STACK_UNWIND (frame, -1, ENOENT, NULL, NULL, NULL);
			return 0;
		}

		gf_log (this->name, GF_LOG_WARNING,
			"linking file %s existing on %s to %s (hash)",
			loc->path, cached_subvol->name, hashed_subvol->name);

		dht_linkfile_create (frame, dht_lookup_linkfile_create_cbk,
				     cached_subvol, hashed_subvol, loc);
	}

	return 0;
}


int
dht_lookup_everywhere (call_frame_t *frame, xlator_t *this, loc_t *loc)
{
	dht_conf_t     *conf = NULL;
	dht_local_t    *local = NULL;
	int             i = 0;
	int             call_cnt = 0;

	conf = this->private;
	local = frame->local;

	call_cnt = conf->subvolume_cnt;
	local->call_cnt = call_cnt;

	if (!local->inode)
		local->inode = inode_ref (loc->inode);

	for (i = 0; i < call_cnt; i++) {
		STACK_WIND (frame, dht_lookup_everywhere_cbk,
			    conf->subvolumes[i],
			    conf->subvolumes[i]->fops->lookup,
			    loc, 1);
	}

	return 0;
}


int
dht_lookup_linkfile_cbk (call_frame_t *frame, void *cookie,
                         xlator_t *this, int op_ret, int op_errno,
                         inode_t *inode, struct stat *stbuf, dict_t *xattr)
{
        call_frame_t *prev = NULL;
	dht_local_t  *local = NULL;
	dht_layout_t *layout = NULL;
	xlator_t     *subvol = NULL;
	loc_t        *loc = NULL;

        prev   = cookie;
	subvol = prev->this;

	local  = frame->local;
	loc    = &local->loc;

        if (op_ret == -1) {
		gf_log (this->name, GF_LOG_WARNING,
			"lookup of %s on %s (following linkfile) failed (%s)",
			local->loc.path, subvol->name, strerror (op_errno));

		dht_lookup_everywhere (frame, this, loc);
		return 0;
	}

        /* TODO: assert type is non-dir and non-linkfile */

	stbuf->st_mode |= S_ISVTX;
        dht_itransform (this, prev->this, stbuf->st_ino, &stbuf->st_ino);

	layout = dht_layout_for_subvol (this, prev->this);
	if (!layout) {
		gf_log (this->name, GF_LOG_ERROR,
			"no pre-set layout for subvolume %s",
			prev->this->name);
		op_ret   = -1;
		op_errno = EINVAL;
		goto out;
	}

	inode_ctx_set (inode, this, layout);

out:
        DHT_STACK_UNWIND (frame, op_ret, op_errno, inode, stbuf, xattr);

        return 0;
}


int
dht_lookup_cbk (call_frame_t *frame, void *cookie, xlator_t *this,
                int op_ret, int op_errno,
                inode_t *inode, struct stat *stbuf, dict_t *xattr)
{
	dht_layout_t *layout      = NULL;
        char          is_linkfile = 0;
        char          is_dir      = 0;
        xlator_t     *subvol      = NULL;
        dht_conf_t   *conf        = NULL;
        dht_local_t  *local       = NULL;
        loc_t        *loc         = NULL;
        int           i           = 0;
        call_frame_t *prev        = NULL;
	int           call_cnt    = 0;


        conf  = this->private;

        prev  = cookie;
        local = frame->local;
        loc   = &local->loc;

	if (ENTRY_MISSING (op_ret, op_errno)) {
		if (conf->search_unhashed) {
			local->op_errno = ENOENT;
			dht_lookup_everywhere (frame, this, loc);
			return 0;
		}
	}

        if (op_ret == -1)
                goto out;

        is_linkfile = check_is_linkfile (inode, stbuf, xattr);
        is_dir      = check_is_dir (inode, stbuf, xattr);

        if (!is_dir && !is_linkfile) {
                /* non-directory and not a linkfile */

		dht_itransform (this, prev->this, stbuf->st_ino,
				&stbuf->st_ino);

		layout = dht_layout_for_subvol (this, prev->this);
		if (!layout) {
			gf_log (this->name, GF_LOG_ERROR,
				"no pre-set layout for subvolume %s",
				prev->this->name);
			op_ret   = -1;
			op_errno = EINVAL;
			goto out;
		}

                inode_ctx_set (inode, this, layout);
                goto out;
        }

        if (is_dir) {
                call_cnt        = conf->subvolume_cnt;
		local->call_cnt = call_cnt;

                local->inode = inode_ref (inode);
                local->xattr = dict_ref (xattr);

		local->op_ret = 0;
		local->op_errno = 0;

		local->layout = dht_layout_new (this, conf->subvolume_cnt);
		if (!local->layout) {
			op_ret   = -1;
			op_errno = ENOMEM;
			gf_log (this->name, GF_LOG_ERROR,
				"memory allocation failed :(");
			goto out;
		}

                for (i = 0; i < call_cnt; i++) {
                        STACK_WIND (frame, dht_lookup_dir_cbk,
                                    conf->subvolumes[i],
                                    conf->subvolumes[i]->fops->lookup,
                                    &local->loc, 1);
                }
        }

        if (is_linkfile) {
                subvol = dht_linkfile_subvol (this, inode, stbuf, xattr);

                if (!subvol) {
                        gf_log (this->name, GF_LOG_WARNING,
                                "linkfile not having link subvolume. path=%s",
                                loc->path);
			dht_lookup_everywhere (frame, this, loc);
			return 0;
                }

		STACK_WIND (frame, dht_lookup_linkfile_cbk,
			    subvol, subvol->fops->lookup,
			    &local->loc, 1);
        }

        return 0;

out:
        DHT_STACK_UNWIND (frame, op_ret, op_errno, inode, stbuf, xattr);
        return 0;
}


int
dht_lookup (call_frame_t *frame, xlator_t *this,
            loc_t *loc, int need_xattr)
{
        xlator_t     *subvol = NULL;
        xlator_t     *hashed_subvol = NULL;
        xlator_t     *cached_subvol = NULL;
        dht_local_t  *local  = NULL;
        int           ret    = -1;
        int           op_errno = -1;
	dht_layout_t *layout = NULL;
	int           i = 0;
	int           call_cnt = 0;


        VALIDATE_OR_GOTO (frame, err);
        VALIDATE_OR_GOTO (this, err);
        VALIDATE_OR_GOTO (loc, err);
        VALIDATE_OR_GOTO (loc->inode, err);
        VALIDATE_OR_GOTO (loc->path, err);

        local = dht_local_init (frame);
	if (!local) {
		op_errno = ENOMEM;
		gf_log (this->name, GF_LOG_ERROR,
			"memory allocation failed :(");
		goto err;
	}

        ret = loc_dup (loc, &local->loc);
        if (ret == -1) {
                op_errno = errno;
                gf_log (this->name, GF_LOG_ERROR,
                        "copying location failed for path=%s",
                        loc->path);
                goto err;
        }

	hashed_subvol = dht_subvol_get_hashed (this, loc);
	cached_subvol = dht_subvol_get_cached (this, loc->inode);

	local->cached_subvol = cached_subvol;
	local->hashed_subvol = hashed_subvol;

        if (is_revalidate (loc)) {
		layout = dht_layout_get (this, loc->inode);

                if (!layout) {
                        gf_log (this->name, GF_LOG_ERROR,
                                "revalidate without cache. path=%s",
                                loc->path);
                        op_errno = EINVAL;
                        goto err;
                }

		local->inode    = inode_ref (loc->inode);
		local->st_ino   = loc->inode->ino;

		local->call_cnt = layout->cnt;
		call_cnt = local->call_cnt;
		
		for (i = 0; i < layout->cnt; i++) {
			subvol = layout->list[i].xlator;

			STACK_WIND (frame, dht_revalidate_cbk,
				    subvol, subvol->fops->lookup,
				    loc, need_xattr);

			if (!--call_cnt)
				break;
		}
        } else {
                if (!hashed_subvol) {
                        gf_log (this->name, GF_LOG_ERROR,
                                "no subvolume in layout for path=%s",
                                loc->path);
                        op_errno = EINVAL;
                        goto err;
                }

                STACK_WIND (frame, dht_lookup_cbk,
                            hashed_subvol, hashed_subvol->fops->lookup,
                            loc, 1);
        }

        return 0;

err:
	op_errno = (op_errno == -1) ? errno : op_errno;
        DHT_STACK_UNWIND (frame, -1, op_errno, NULL, NULL, NULL);
        return 0;
}


int
dht_attr_cbk (call_frame_t *frame, void *cookie, xlator_t *this,
	      int op_ret, int op_errno, struct stat *stbuf)
{
	dht_local_t  *local = NULL;
	int           this_call_cnt = 0;
	call_frame_t *prev = NULL;


	local = frame->local;
	prev = cookie;

	LOCK (&frame->lock);
	{
		if (op_ret == -1) {
			local->op_errno = op_errno;
			gf_log (this->name, GF_LOG_ERROR,
				"subvolume %s returned -1 (%s)",
				prev->this->name, strerror (op_errno));
			goto unlock;
		}

		dht_stat_merge (this, &local->stbuf, stbuf, prev->this);
		
		if (local->inode)
			local->stbuf.st_ino = local->inode->ino;
		local->op_ret = 0;
	}
unlock:
	UNLOCK (&frame->lock);

	this_call_cnt = dht_frame_return (frame);
	if (is_last_call (this_call_cnt))
		DHT_STACK_UNWIND (frame, local->op_ret, local->op_errno,
				  &local->stbuf);

        return 0;
}


int
dht_stat (call_frame_t *frame, xlator_t *this,
	  loc_t *loc)
{
	xlator_t     *subvol = NULL;
        int           op_errno = -1;
	dht_local_t  *local = NULL;
	dht_layout_t *layout = NULL;
	int           i = 0;


        VALIDATE_OR_GOTO (frame, err);
        VALIDATE_OR_GOTO (this, err);
        VALIDATE_OR_GOTO (loc, err);
        VALIDATE_OR_GOTO (loc->inode, err);
        VALIDATE_OR_GOTO (loc->path, err);

	layout = dht_layout_get (this, loc->inode);
	if (!layout) {
		gf_log (this->name, GF_LOG_ERROR,
			"no layout for path=%s", loc->path);
		op_errno = EINVAL;
		goto err;
	}

	local = dht_local_init (frame);
	if (!local) {
		op_errno = ENOMEM;
		gf_log (this->name, GF_LOG_ERROR,
			"memory allocation failed :(");
		goto err;
	}

	local->inode = inode_ref (loc->inode);
	local->call_cnt = layout->cnt;

	for (i = 0; i < layout->cnt; i++) {
		subvol = layout->list[i].xlator;

		STACK_WIND (frame, dht_attr_cbk,
			    subvol, subvol->fops->stat,
			    loc);
	}

	return 0;

err:
	op_errno = (op_errno == -1) ? errno : op_errno;
	DHT_STACK_UNWIND (frame, -1, op_errno, NULL);

	return 0;
}


int
dht_fstat (call_frame_t *frame, xlator_t *this,
	   fd_t *fd)
{
	xlator_t     *subvol = NULL;
        int           op_errno = -1;
	dht_local_t  *local = NULL;
	dht_layout_t *layout = NULL;
	int           i = 0;


        VALIDATE_OR_GOTO (frame, err);
        VALIDATE_OR_GOTO (this, err);
        VALIDATE_OR_GOTO (fd, err);

	layout = dht_layout_get (this, fd->inode);
	if (!layout) {
		gf_log (this->name, GF_LOG_ERROR,
			"no layout for fd=%p", fd);
		op_errno = EINVAL;
		goto err;
	}

	local = dht_local_init (frame);
	if (!local) {
		op_errno = ENOMEM;
		gf_log (this->name, GF_LOG_ERROR,
			"local allocation failed :(");
		goto err;
	}

	local->inode    = inode_ref (fd->inode);
	local->call_cnt = layout->cnt;;

	for (i = 0; i < layout->cnt; i++) {
		subvol = layout->list[i].xlator;
		STACK_WIND (frame, dht_attr_cbk,
			    subvol, subvol->fops->fstat,
			    fd);
	}

	return 0;

err:
	op_errno = (op_errno == -1) ? errno : op_errno;
	DHT_STACK_UNWIND (frame, -1, op_errno, NULL);

	return 0;
}


int
dht_chmod (call_frame_t *frame, xlator_t *this,
	   loc_t *loc, mode_t mode)
{
	dht_layout_t *layout = NULL;
	dht_local_t  *local  = NULL;
        int           op_errno = -1;
	int           i = -1;


        VALIDATE_OR_GOTO (frame, err);
        VALIDATE_OR_GOTO (this, err);
        VALIDATE_OR_GOTO (loc, err);
        VALIDATE_OR_GOTO (loc->inode, err);
        VALIDATE_OR_GOTO (loc->path, err);

	layout = dht_layout_get (this, loc->inode);

	if (!layout) {
		gf_log (this->name, GF_LOG_ERROR,
			"no layout for path=%s", loc->path);
		op_errno = EINVAL;
		goto err;
	}

	if (!layout_is_sane (layout)) {
		gf_log (this->name, GF_LOG_ERROR,
			"layout is not sane for path=%s", loc->path);
		op_errno = EINVAL;
		goto err;
	}

	local = dht_local_init (frame);
	if (!local) {
		op_errno = ENOMEM;
		gf_log (this->name, GF_LOG_ERROR,
			"memory allocation failed :(");
		goto err;
	}

	local->inode = inode_ref (loc->inode);
	local->call_cnt = layout->cnt;

	for (i = 0; i < layout->cnt; i++) {
		STACK_WIND (frame, dht_attr_cbk,
			    layout->list[i].xlator,
			    layout->list[i].xlator->fops->chmod,
			    loc, mode);
	}

	return 0;

err:
	op_errno = (op_errno == -1) ? errno : op_errno;
	DHT_STACK_UNWIND (frame, -1, op_errno, NULL);

	return 0;
}


int
dht_chown (call_frame_t *frame, xlator_t *this,
	   loc_t *loc, uid_t uid, gid_t gid)
{
	dht_layout_t *layout = NULL;
	dht_local_t  *local  = NULL;
        int           op_errno = -1;
	int           i = -1;


        VALIDATE_OR_GOTO (frame, err);
        VALIDATE_OR_GOTO (this, err);
        VALIDATE_OR_GOTO (loc, err);
        VALIDATE_OR_GOTO (loc->inode, err);
        VALIDATE_OR_GOTO (loc->path, err);

	layout = dht_layout_get (this, loc->inode);
	if (!layout) {
		gf_log (this->name, GF_LOG_ERROR,
			"no layout for path=%s", loc->path);
		op_errno = EINVAL;
		goto err;
	}

	if (!layout_is_sane (layout)) {
		gf_log (this->name, GF_LOG_ERROR,
			"layout is not sane for path=%s", loc->path);
		op_errno = EINVAL;
		goto err;
	}

	local = dht_local_init (frame);
	if (!local) {
		op_errno = ENOMEM;
		gf_log (this->name, GF_LOG_ERROR,
			"memory allocation failed :(");
		goto err;
	}

	local->inode = inode_ref (loc->inode);
	local->call_cnt = layout->cnt;

	for (i = 0; i < layout->cnt; i++) {
		STACK_WIND (frame, dht_attr_cbk,
			    layout->list[i].xlator,
			    layout->list[i].xlator->fops->chown,
			    loc, uid, gid);
	}

	return 0;

err:
	op_errno = (op_errno == -1) ? errno : op_errno;
	DHT_STACK_UNWIND (frame, -1, op_errno, NULL);

	return 0;
}


int
dht_fchmod (call_frame_t *frame, xlator_t *this,
	    fd_t *fd, mode_t mode)
{
	dht_layout_t *layout = NULL;
	dht_local_t  *local  = NULL;
        int           op_errno = -1;
	int           i = -1;


        VALIDATE_OR_GOTO (frame, err);
        VALIDATE_OR_GOTO (this, err);
        VALIDATE_OR_GOTO (fd, err);


	layout = dht_layout_get (this, fd->inode);
	if (!layout) {
		gf_log (this->name, GF_LOG_ERROR,
			"no layout for fd=%p", fd);
		op_errno = EINVAL;
		goto err;
	}

	if (!layout_is_sane (layout)) {
		gf_log (this->name, GF_LOG_ERROR,
			"layout is not sane for fd=%p", fd);
		op_errno = EINVAL;
		goto err;
	}

	local = dht_local_init (frame);
	if (!local) {
		op_errno = ENOMEM;
		gf_log (this->name, GF_LOG_ERROR,
			"memory allocation failed :(");
		goto err;
	}

	local->inode = inode_ref (fd->inode);
	local->call_cnt = layout->cnt;

	for (i = 0; i < layout->cnt; i++) {
		STACK_WIND (frame, dht_attr_cbk,
			    layout->list[i].xlator,
			    layout->list[i].xlator->fops->fchmod,
			    fd, mode);
	}

	return 0;

err:
	op_errno = (op_errno == -1) ? errno : op_errno;
	DHT_STACK_UNWIND (frame, -1, op_errno, NULL);

	return 0;
}


int
dht_fchown (call_frame_t *frame, xlator_t *this,
	    fd_t *fd, uid_t uid, gid_t gid)
{
	dht_layout_t *layout = NULL;
	dht_local_t  *local  = NULL;
        int           op_errno = -1;
	int           i = -1;


        VALIDATE_OR_GOTO (frame, err);
        VALIDATE_OR_GOTO (this, err);
        VALIDATE_OR_GOTO (fd, err);

	layout = dht_layout_get (this, fd->inode);
	if (!layout) {
		gf_log (this->name, GF_LOG_ERROR,
			"no layout for fd=%p", fd);
		op_errno = EINVAL;
		goto err;
	}

	if (!layout_is_sane (layout)) {
		gf_log (this->name, GF_LOG_ERROR,
			"layout is not sane for fd=%p", fd);
		op_errno = EINVAL;
		goto err;
	}

	local = dht_local_init (frame);
	if (!local) {
		op_errno = ENOMEM;
		gf_log (this->name, GF_LOG_ERROR,
			"memory allocation failed :(");
		goto err;
	}

	local->inode = inode_ref (fd->inode);
	local->call_cnt = layout->cnt;

	for (i = 0; i < layout->cnt; i++) {
		STACK_WIND (frame, dht_attr_cbk,
			    layout->list[i].xlator,
			    layout->list[i].xlator->fops->fchown,
			    fd, uid, gid);
	}

	return 0;

err:
	op_errno = (op_errno == -1) ? errno : op_errno;
	DHT_STACK_UNWIND (frame, -1, op_errno, NULL);

	return 0;
}


int
dht_utimens (call_frame_t *frame, xlator_t *this,
	     loc_t *loc, struct timespec tv[2])
{
	dht_layout_t *layout = NULL;
	dht_local_t  *local  = NULL;
        int           op_errno = -1;
	int           i = -1;


        VALIDATE_OR_GOTO (frame, err);
        VALIDATE_OR_GOTO (this, err);
        VALIDATE_OR_GOTO (loc, err);
        VALIDATE_OR_GOTO (loc->inode, err);
        VALIDATE_OR_GOTO (loc->path, err);

	layout = dht_layout_get (this, loc->inode);
	if (!layout) {
		gf_log (this->name, GF_LOG_ERROR,
			"no layout for path=%s", loc->path);
		op_errno = EINVAL;
		goto err;
	}

	if (!layout_is_sane (layout)) {
		gf_log (this->name, GF_LOG_ERROR,
			"layout is not sane for path=%s", loc->path);
		op_errno = EINVAL;
		goto err;
	}

	local = dht_local_init (frame);
	if (!local) {
		op_errno = ENOMEM;
		gf_log (this->name, GF_LOG_ERROR,
			"memory allocation failed :(");
		goto err;
	}

	local->inode = inode_ref (loc->inode);
	local->call_cnt = layout->cnt;

	for (i = 0; i < layout->cnt; i++) {
		STACK_WIND (frame, dht_attr_cbk,
			    layout->list[i].xlator,
			    layout->list[i].xlator->fops->utimens,
			    loc, tv);
	}

	return 0;

err:
	op_errno = (op_errno == -1) ? errno : op_errno;
	DHT_STACK_UNWIND (frame, -1, op_errno, NULL);

	return 0;
}


int
dht_truncate (call_frame_t *frame, xlator_t *this,
	      loc_t *loc, off_t offset)
{
	xlator_t     *subvol = NULL;
        int           op_errno = -1;
	dht_local_t  *local = NULL;


        VALIDATE_OR_GOTO (frame, err);
        VALIDATE_OR_GOTO (this, err);
        VALIDATE_OR_GOTO (loc, err);
        VALIDATE_OR_GOTO (loc->inode, err);
        VALIDATE_OR_GOTO (loc->path, err);

	subvol = dht_subvol_get_cached (this, loc->inode);
	if (!subvol) {
		gf_log (this->name, GF_LOG_ERROR,
			"no cached subvolume for path=%s", loc->path);
		op_errno = EINVAL;
		goto err;
	}

	local = dht_local_init (frame);
	if (!local) {
		op_errno = ENOMEM;
		gf_log (this->name, GF_LOG_ERROR,
			"memory allocation failed :(");
		goto err;
	}

	local->inode = inode_ref (loc->inode);
	local->call_cnt = 1;

	STACK_WIND (frame, dht_attr_cbk,
		    subvol, subvol->fops->truncate,
		    loc, offset);

	return 0;

err:
	op_errno = (op_errno == -1) ? errno : op_errno;
	DHT_STACK_UNWIND (frame, -1, op_errno, NULL);

	return 0;
}


int
dht_ftruncate (call_frame_t *frame, xlator_t *this,
	       fd_t *fd, off_t offset)
{
	xlator_t     *subvol = NULL;
        int           op_errno = -1;
	dht_local_t  *local = NULL;


        VALIDATE_OR_GOTO (frame, err);
        VALIDATE_OR_GOTO (this, err);
        VALIDATE_OR_GOTO (fd, err);

	subvol = dht_subvol_get_cached (this, fd->inode);
	if (!subvol) {
		gf_log (this->name, GF_LOG_ERROR,
			"no cached subvolume for fd=%p", fd);
		op_errno = EINVAL;
		goto err;
	}

	local = dht_local_init (frame);
	if (!local) {
		op_errno = ENOMEM;
		gf_log (this->name, GF_LOG_ERROR,
			"memory allocation failed :(");
		goto err;
	}

	local->inode = inode_ref (fd->inode);
	local->call_cnt = 1;

	STACK_WIND (frame, dht_attr_cbk,
		    subvol, subvol->fops->ftruncate,
		    fd, offset);

	return 0;

err:
	op_errno = (op_errno == -1) ? errno : op_errno;
	DHT_STACK_UNWIND (frame, -1, op_errno, NULL);

	return 0;
}


int
dht_err_cbk (call_frame_t *frame, void *cookie, xlator_t *this,
	     int op_ret, int op_errno)
{
	dht_local_t  *local = NULL;
	int           this_call_cnt = 0;
	call_frame_t *prev = NULL;


	local = frame->local;
	prev = cookie;

	LOCK (&frame->lock);
	{
		if (op_ret == -1) {
			local->op_errno = op_errno;
			gf_log (this->name, GF_LOG_ERROR,
				"subvolume %s returned -1 (%s)",
				prev->this->name, strerror (op_errno));
			goto unlock;
		}

		local->op_ret = 0;
	}
unlock:
	UNLOCK (&frame->lock);

	this_call_cnt = dht_frame_return (frame);
	if (is_last_call (this_call_cnt))
		DHT_STACK_UNWIND (frame, local->op_ret, local->op_errno);

        return 0;
}


int
dht_access (call_frame_t *frame, xlator_t *this,
	    loc_t *loc, int32_t mask)
{
	xlator_t     *subvol = NULL;
        int           op_errno = -1;
	dht_local_t  *local = NULL;


        VALIDATE_OR_GOTO (frame, err);
        VALIDATE_OR_GOTO (this, err);
        VALIDATE_OR_GOTO (loc, err);
        VALIDATE_OR_GOTO (loc->inode, err);
        VALIDATE_OR_GOTO (loc->path, err);

	subvol = dht_subvol_get_cached (this, loc->inode);
	if (!subvol) {
		gf_log (this->name, GF_LOG_ERROR,
			"no cached subvolume for path=%s", loc->path);
		op_errno = EINVAL;
		goto err;
	}

	local = dht_local_init (frame);
	if (!local) {
		op_errno = ENOMEM;
		gf_log (this->name, GF_LOG_ERROR,
			"memory allocation failed :(");
		goto err;
	}

	local->call_cnt = 1;

	STACK_WIND (frame, dht_err_cbk,
		    subvol, subvol->fops->access,
		    loc, mask);

	return 0;

err:
	op_errno = (op_errno == -1) ? errno : op_errno;
	DHT_STACK_UNWIND (frame, -1, op_errno);

	return 0;
}


int
dht_readlink_cbk (call_frame_t *frame, void *cookie, xlator_t *this,
		  int op_ret, int op_errno, const char *path)
{
        DHT_STACK_UNWIND (frame, op_ret, op_errno, path);

        return 0;
}


int
dht_readlink (call_frame_t *frame, xlator_t *this,
	      loc_t *loc, size_t size)
{
	xlator_t     *subvol = NULL;
        int           op_errno = -1;


        VALIDATE_OR_GOTO (frame, err);
        VALIDATE_OR_GOTO (this, err);
        VALIDATE_OR_GOTO (loc, err);
        VALIDATE_OR_GOTO (loc->inode, err);
        VALIDATE_OR_GOTO (loc->path, err);

	subvol = dht_subvol_get_cached (this, loc->inode);
	if (!subvol) {
		gf_log (this->name, GF_LOG_ERROR,
			"no cached subvolume for path=%s", loc->path);
		op_errno = EINVAL;
		goto err;
	}

	STACK_WIND (frame, dht_readlink_cbk,
		    subvol, subvol->fops->readlink,
		    loc, size);

	return 0;

err:
	op_errno = (op_errno == -1) ? errno : op_errno;
	DHT_STACK_UNWIND (frame, -1, op_errno, NULL);

	return 0;
}


int
dht_getxattr_cbk (call_frame_t *frame, void *cookie, xlator_t *this,
		  int op_ret, int op_errno, dict_t *xattr)
{
        DHT_STACK_UNWIND (frame, op_ret, op_errno, xattr);

        return 0;
}


int
dht_getxattr (call_frame_t *frame, xlator_t *this,
	      loc_t *loc, const char *key)
{
	xlator_t     *subvol = NULL;
        int           op_errno = -1;


        VALIDATE_OR_GOTO (frame, err);
        VALIDATE_OR_GOTO (this, err);
        VALIDATE_OR_GOTO (loc, err);
        VALIDATE_OR_GOTO (loc->inode, err);
        VALIDATE_OR_GOTO (loc->path, err);

	subvol = dht_subvol_get_cached (this, loc->inode);
	if (!subvol) {
		gf_log (this->name, GF_LOG_ERROR,
			"no cached subvolume for path=%s", loc->path);
		op_errno = EINVAL;
		goto err;
	}

	STACK_WIND (frame, dht_getxattr_cbk,
		    subvol, subvol->fops->getxattr,
		    loc, key);

	return 0;

err:
	op_errno = (op_errno == -1) ? errno : op_errno;
	DHT_STACK_UNWIND (frame, -1, op_errno, NULL);

	return 0;
}


int
dht_setxattr (call_frame_t *frame, xlator_t *this,
	      loc_t *loc, dict_t *xattr, int flags)
{
	xlator_t     *subvol = NULL;
        int           op_errno = -1;
	dht_local_t  *local = NULL;


        VALIDATE_OR_GOTO (frame, err);
        VALIDATE_OR_GOTO (this, err);
        VALIDATE_OR_GOTO (loc, err);
        VALIDATE_OR_GOTO (loc->inode, err);
        VALIDATE_OR_GOTO (loc->path, err);

	subvol = dht_subvol_get_cached (this, loc->inode);
	if (!subvol) {
		gf_log (this->name, GF_LOG_ERROR,
			"no cached subvolume for path=%s", loc->path);
		op_errno = EINVAL;
		goto err;
	}

	local = dht_local_init (frame);
	if (!local) {
		op_errno = ENOMEM;
		gf_log (this->name, GF_LOG_ERROR,
			"memory allocation failed :(");
		goto err;
	}

	local->call_cnt = 1;

	STACK_WIND (frame, dht_err_cbk,
		    subvol, subvol->fops->setxattr,
		    loc, xattr, flags);

	return 0;

err:
	op_errno = (op_errno == -1) ? errno : op_errno;
	DHT_STACK_UNWIND (frame, -1, op_errno, NULL);

	return 0;
}


int
dht_removexattr (call_frame_t *frame, xlator_t *this,
		 loc_t *loc, const char *key)
{
	xlator_t     *subvol = NULL;
        int           op_errno = -1;
	dht_local_t  *local = NULL;


        VALIDATE_OR_GOTO (frame, err);
        VALIDATE_OR_GOTO (this, err);
        VALIDATE_OR_GOTO (loc, err);
        VALIDATE_OR_GOTO (loc->inode, err);
        VALIDATE_OR_GOTO (loc->path, err);

	subvol = dht_subvol_get_cached (this, loc->inode);
	if (!subvol) {
		gf_log (this->name, GF_LOG_ERROR,
			"no cached subvolume for path=%s", loc->path);
		op_errno = EINVAL;
		goto err;
	}

	local = dht_local_init (frame);
	if (!local) {
		op_errno = ENOMEM;
		gf_log (this->name, GF_LOG_ERROR,
			"memory allocation failed :(");
		goto err;
	}

	local->call_cnt = 1;

	STACK_WIND (frame, dht_err_cbk,
		    subvol, subvol->fops->removexattr,
		    loc, key);

	return 0;

err:
	op_errno = (op_errno == -1) ? errno : op_errno;
	DHT_STACK_UNWIND (frame, -1, op_errno, NULL);

	return 0;
}


int
dht_fd_cbk (call_frame_t *frame, void *cookie, xlator_t *this,
	      int op_ret, int op_errno, fd_t *fd)
{
	dht_local_t  *local = NULL;
	int           this_call_cnt = 0;
	call_frame_t *prev = NULL;


	local = frame->local;
	prev = cookie;

	LOCK (&frame->lock);
	{
		if (op_ret == -1) {
			local->op_errno = op_errno;
			gf_log (this->name, GF_LOG_ERROR,
				"subvolume %s returned -1 (%s)",
				prev->this->name, strerror (op_errno));
			goto unlock;
		}

		local->op_ret = 0;
	}
unlock:
	UNLOCK (&frame->lock);

	this_call_cnt = dht_frame_return (frame);
	if (is_last_call (this_call_cnt))
		DHT_STACK_UNWIND (frame, local->op_ret, local->op_errno,
				  local->fd);

        return 0;
}


int
dht_open (call_frame_t *frame, xlator_t *this,
	  loc_t *loc, int flags, fd_t *fd)
{
	xlator_t     *subvol = NULL;
	int           ret = -1;
        int           op_errno = -1;
	dht_local_t  *local = NULL;


        VALIDATE_OR_GOTO (frame, err);
        VALIDATE_OR_GOTO (this, err);
        VALIDATE_OR_GOTO (fd, err);

	subvol = dht_subvol_get_cached (this, fd->inode);
	if (!subvol) {
		gf_log (this->name, GF_LOG_ERROR,
			"no cached subvolume for fd=%p", fd);
		op_errno = EINVAL;
		goto err;
	}

	local = dht_local_init (frame);
	if (!local) {
		op_errno = ENOMEM;
		gf_log (this->name, GF_LOG_ERROR,
			"memory allocation failed :(");
		goto err;
	}

	local->fd = fd_ref (fd);
	ret = loc_dup (loc, &local->loc);
	if (ret == -1) {
		op_errno = ENOMEM;
		gf_log (this->name, GF_LOG_ERROR,
			"memory allocation failed :(");
		goto err;
	}

	local->call_cnt = 1;

	STACK_WIND (frame, dht_fd_cbk,
		    subvol, subvol->fops->open,
		    loc, flags, fd);

	return 0;

err:
	op_errno = (op_errno == -1) ? errno : op_errno;
	DHT_STACK_UNWIND (frame, -1, op_errno, NULL);

	return 0;
}


int
dht_readv_cbk (call_frame_t *frame, void *cookie, xlator_t *this,
	       int op_ret, int op_errno,
	       struct iovec *vector, int count, struct stat *stbuf)
{
        DHT_STACK_UNWIND (frame, op_ret, op_errno, vector, count, stbuf);

        return 0;
}


int
dht_readv (call_frame_t *frame, xlator_t *this,
	   fd_t *fd, size_t size, off_t off)
{
	xlator_t     *subvol = NULL;
        int           op_errno = -1;


        VALIDATE_OR_GOTO (frame, err);
        VALIDATE_OR_GOTO (this, err);
        VALIDATE_OR_GOTO (fd, err);

	subvol = dht_subvol_get_cached (this, fd->inode);
	if (!subvol) {
		gf_log (this->name, GF_LOG_ERROR,
			"no cached subvolume for fd=%p", fd);
		op_errno = EINVAL;
		goto err;
	}

	STACK_WIND (frame, dht_readv_cbk,
		    subvol, subvol->fops->readv,
		    fd, size, off);

	return 0;

err:
	op_errno = (op_errno == -1) ? errno : op_errno;
	DHT_STACK_UNWIND (frame, -1, op_errno, NULL, 0, NULL);

	return 0;
}


int
dht_writev_cbk (call_frame_t *frame, void *cookie, xlator_t *this,
		int op_ret, int op_errno, struct stat *stbuf)
{
        DHT_STACK_UNWIND (frame, op_ret, op_errno, stbuf);

        return 0;
}


int
dht_writev (call_frame_t *frame, xlator_t *this,
	    fd_t *fd, struct iovec *vector, int count, off_t off)
{
	xlator_t     *subvol = NULL;
        int           op_errno = -1;


        VALIDATE_OR_GOTO (frame, err);
        VALIDATE_OR_GOTO (this, err);
        VALIDATE_OR_GOTO (fd, err);

	subvol = dht_subvol_get_cached (this, fd->inode);
	if (!subvol) {
		gf_log (this->name, GF_LOG_ERROR,
			"no cached subvolume for fd=%p", fd);
		op_errno = EINVAL;
		goto err;
	}

	STACK_WIND (frame, dht_writev_cbk,
		    subvol, subvol->fops->writev,
		    fd, vector, count, off);

	return 0;

err:
	op_errno = (op_errno == -1) ? errno : op_errno;
	DHT_STACK_UNWIND (frame, -1, op_errno, NULL, 0);

	return 0;
}


int
dht_flush (call_frame_t *frame, xlator_t *this, fd_t *fd)
{
	xlator_t     *subvol = NULL;
        int           op_errno = -1;
	dht_local_t  *local = NULL;


        VALIDATE_OR_GOTO (frame, err);
        VALIDATE_OR_GOTO (this, err);
        VALIDATE_OR_GOTO (fd, err);

	subvol = dht_subvol_get_cached (this, fd->inode);
	if (!subvol) {
		gf_log (this->name, GF_LOG_ERROR,
			"no cached subvolume for fd=%p", fd);
		op_errno = EINVAL;
		goto err;
	}

	local = dht_local_init (frame);
	if (!local) {
		op_errno = ENOMEM;
		gf_log (this->name, GF_LOG_ERROR,
			"memory allocation failed :(");
		goto err;
	}

	local->fd = fd_ref (fd);
	local->call_cnt = 1;

	STACK_WIND (frame, dht_err_cbk,
		    subvol, subvol->fops->flush, fd);

	return 0;

err:
	op_errno = (op_errno == -1) ? errno : op_errno;
	DHT_STACK_UNWIND (frame, -1, op_errno);

	return 0;
}


int
dht_fsync (call_frame_t *frame, xlator_t *this,
	   fd_t *fd, int datasync)
{
	xlator_t     *subvol = NULL;
        int           op_errno = -1;
	dht_local_t  *local = NULL;


        VALIDATE_OR_GOTO (frame, err);
        VALIDATE_OR_GOTO (this, err);
        VALIDATE_OR_GOTO (fd, err);

	subvol = dht_subvol_get_cached (this, fd->inode);
	if (!subvol) {
		gf_log (this->name, GF_LOG_ERROR,
			"no cached subvolume for fd=%p", fd);
		op_errno = EINVAL;
		goto err;
	}

	local = dht_local_init (frame);
	if (!local) {
		op_errno = ENOMEM;
		gf_log (this->name, GF_LOG_ERROR,
			"memory allocatoin failed :(");
		goto err;
	}
	local->call_cnt = 1;

	STACK_WIND (frame, dht_err_cbk,
		    subvol, subvol->fops->fsync,
		    fd, datasync);

	return 0;

err:
	op_errno = (op_errno == -1) ? errno : op_errno;
	DHT_STACK_UNWIND (frame, -1, op_errno);

	return 0;
}


int
dht_lk_cbk (call_frame_t *frame, void *cookie, xlator_t *this,
	    int op_ret, int op_errno, struct flock *flock)
{
        DHT_STACK_UNWIND (frame, op_ret, op_errno, flock);

        return 0;
}


int
dht_lk (call_frame_t *frame, xlator_t *this,
	fd_t *fd, int cmd, struct flock *flock)
{
	xlator_t     *subvol = NULL;
        int           op_errno = -1;


        VALIDATE_OR_GOTO (frame, err);
        VALIDATE_OR_GOTO (this, err);
        VALIDATE_OR_GOTO (fd, err);

	subvol = dht_subvol_get_cached (this, fd->inode);
	if (!subvol) {
		gf_log (this->name, GF_LOG_ERROR,
			"no cached subvolume for fd=%p", fd);
		op_errno = EINVAL;
		goto err;
	}

	STACK_WIND (frame, dht_lk_cbk,
		    subvol, subvol->fops->lk,
		    fd, cmd, flock);

	return 0;

err:
	op_errno = (op_errno == -1) ? errno : op_errno;
	DHT_STACK_UNWIND (frame, -1, op_errno, NULL);

	return 0;
}

/* gf_lk no longer exists 
int
dht_gf_lk_cbk (call_frame_t *frame, void *cookie, xlator_t *this,
	    int op_ret, int op_errno, struct flock *flock)
{
        DHT_STACK_UNWIND (frame, op_ret, op_errno, flock);

        return 0;
}


int
dht_gf_lk (call_frame_t *frame, xlator_t *this,
	   loc_t *loc, int cmd, struct flock *flock)
{
	xlator_t     *subvol = NULL;
        int           op_errno = -1;


        VALIDATE_OR_GOTO (frame, err);
        VALIDATE_OR_GOTO (this, err);
        VALIDATE_OR_GOTO (fd, err);

	subvol = dht_subvol_get_cached (this, fd->inode);
	if (!subvol) {
		gf_log (this->name, GF_LOG_ERROR,
			"no cached subvolume for fd=%p", fd);
		op_errno = EINVAL;
		goto err;
	}

	STACK_WIND (frame, dht_gf_lk_cbk,
		    subvol, subvol->fops->gf_lk,
		    fd, cmd, flock);

	return 0;

err:
	op_errno = (op_errno == -1) ? errno : op_errno;
	DHT_STACK_UNWIND (frame, -1, op_errno, NULL);

	return 0;
}
*/

int
dht_statfs_cbk (call_frame_t *frame, void *cookie, xlator_t *this,
		int op_ret, int op_errno, struct statvfs *statvfs)
{
	dht_local_t  *local = NULL;
	int           this_call_cnt = 0;


	local = frame->local;

	LOCK (&frame->lock);
	{
		if (op_ret == -1) {
			local->op_errno = op_errno;
			goto unlock;
		}
		local->op_ret = 0;

		/* TODO: normalize sizes */
		local->statvfs.f_bsize    = statvfs->f_bsize;
		local->statvfs.f_frsize   = statvfs->f_frsize;

		local->statvfs.f_blocks  += statvfs->f_blocks;
		local->statvfs.f_bfree   += statvfs->f_bfree;
		local->statvfs.f_bavail  += statvfs->f_bavail;
		local->statvfs.f_files   += statvfs->f_files;
		local->statvfs.f_ffree   += statvfs->f_ffree;
		local->statvfs.f_favail  += statvfs->f_favail;
		local->statvfs.f_fsid     = statvfs->f_fsid;
		local->statvfs.f_flag     = statvfs->f_flag;
		local->statvfs.f_namemax  = statvfs->f_namemax;

	}
unlock:
	UNLOCK (&frame->lock);


	this_call_cnt = dht_frame_return (frame);
	if (is_last_call (this_call_cnt))
		DHT_STACK_UNWIND (frame, local->op_ret, local->op_errno,
				  &local->statvfs);

        return 0;
}


int
dht_statfs (call_frame_t *frame, xlator_t *this, loc_t *loc)
{
	dht_local_t  *local  = NULL;
	dht_conf_t   *conf = NULL;
        int           op_errno = -1;
	int           i = -1;


        VALIDATE_OR_GOTO (frame, err);
        VALIDATE_OR_GOTO (this, err);
        VALIDATE_OR_GOTO (loc, err);
        VALIDATE_OR_GOTO (loc->inode, err);
        VALIDATE_OR_GOTO (loc->path, err);

	conf = this->private;

	local = dht_local_init (frame);
	local->call_cnt = conf->subvolume_cnt;

	for (i = 0; i < conf->subvolume_cnt; i++) {
		STACK_WIND (frame, dht_statfs_cbk,
			    conf->subvolumes[i],
			    conf->subvolumes[i]->fops->statfs, loc);
	}

	return 0;

err:
	op_errno = (op_errno == -1) ? errno : op_errno;
	DHT_STACK_UNWIND (frame, -1, op_errno, NULL);

	return 0;
}


int
dht_opendir (call_frame_t *frame, xlator_t *this, loc_t *loc, fd_t *fd)
{
	dht_local_t  *local  = NULL;
	dht_conf_t   *conf = NULL;
	int           ret = -1;
        int           op_errno = -1;
	int           i = -1;


        VALIDATE_OR_GOTO (frame, err);
        VALIDATE_OR_GOTO (this, err);
        VALIDATE_OR_GOTO (fd, err);

	conf = this->private;

	local = dht_local_init (frame);
	if (!local) {
		op_errno = ENOMEM;
		gf_log (this->name, GF_LOG_ERROR,
			"memory allocation failed :(");
		goto err;
	}

	local->fd = fd_ref (fd);
	ret = loc_dup (loc, &local->loc);
	if (ret == -1) {
		op_errno = ENOMEM;
		gf_log (this->name, GF_LOG_ERROR,
			"memory allocation failed :(");
		goto err;
	}

	local->call_cnt = conf->subvolume_cnt;

	for (i = 0; i < conf->subvolume_cnt; i++) {
		STACK_WIND (frame, dht_fd_cbk,
			    conf->subvolumes[i],
			    conf->subvolumes[i]->fops->opendir,
			    loc, fd);
	}

	return 0;

err:
	op_errno = (op_errno == -1) ? errno : op_errno;
	DHT_STACK_UNWIND (frame, -1, op_errno, NULL);

	return 0;
}


int
dht_readdir_cbk (call_frame_t *frame, void *cookie, xlator_t *this,
		 int op_ret, int op_errno, gf_dirent_t *orig_entries)
{
	dht_local_t  *local = NULL;
	gf_dirent_t   entries;
	gf_dirent_t  *orig_entry = NULL;
	gf_dirent_t  *entry = NULL;
	call_frame_t *prev = NULL;
	xlator_t     *subvol = NULL;
	xlator_t     *next = NULL;
	dht_layout_t *layout = NULL;
	int           count = 0;


	INIT_LIST_HEAD (&entries.list);
	prev = cookie;
	local = frame->local;

	if (op_ret >= 0) {
		/* in case the last subvolme unwinds error, this lets it know not to unwind
		   the error upwards */
		local->op_ret = 0;
	}

	if (op_ret < 0)
		goto done;

	layout = dht_layout_get (this, local->fd->inode);

	list_for_each_entry (orig_entry, &orig_entries->list, list) {
		subvol = dht_layout_search (this, layout, orig_entry->d_name);

		if (!subvol || subvol == prev->this) {
			entry = gf_dirent_for_name (orig_entry->d_name);
			if (!entry) {
				gf_log (this->name, GF_LOG_ERROR,
					"memory allocation failed :(");
				goto unwind;
			}

			dht_itransform (this, subvol, orig_entry->d_ino,
					&entry->d_ino);
			dht_itransform (this, subvol, orig_entry->d_off,
					&entry->d_off);

			entry->d_type = orig_entry->d_type;
			entry->d_len  = orig_entry->d_len;

			list_add_tail (&entry->list, &entries.list);
			count++;
		}
	}
	op_ret = count;

done:
	if (count == 0) {
		next = dht_subvol_next (this, prev->this);
		if (!next) {
			op_ret = local->op_ret;
			goto unwind;
		}

		STACK_WIND (frame, dht_readdir_cbk,
			    next, next->fops->readdir,
			    local->fd, local->size, 0);
		return 0;
	}

unwind:
	DHT_STACK_UNWIND (frame, op_ret, op_errno, &entries);

	gf_dirent_free (&entries);

        return 0;
}


int
dht_readdir (call_frame_t *frame, xlator_t *this,
	     fd_t *fd, size_t size, off_t yoff)
{
	dht_local_t  *local  = NULL;
	dht_conf_t   *conf = NULL;
        int           op_errno = -1;
	xlator_t     *xvol = NULL;
	off_t         xoff = 0;


        VALIDATE_OR_GOTO (frame, err);
        VALIDATE_OR_GOTO (this, err);
        VALIDATE_OR_GOTO (fd, err);

	conf = this->private;

	local = dht_local_init (frame);
	if (!local) {
		gf_log (this->name, GF_LOG_ERROR,
			"memory allocation failed :(");
		op_errno = ENOMEM;
		goto err;
	}

	local->fd = fd_ref (fd);
	local->size = size;

	dht_deitransform (this, yoff, &xvol, (uint64_t *)&xoff);

	/* TODO: do proper readdir */
	STACK_WIND (frame, dht_readdir_cbk,
		    xvol, xvol->fops->readdir,
		    fd, size, xoff);

	return 0;

err:
	op_errno = (op_errno == -1) ? errno : op_errno;
	DHT_STACK_UNWIND (frame, -1, op_errno, NULL);

	return 0;
}


int
dht_fsyncdir_cbk (call_frame_t *frame, void *cookie, xlator_t *this,
		  int op_ret, int op_errno)
{
	dht_local_t  *local = NULL;
	int           this_call_cnt = 0;


	local = frame->local;

	LOCK (&frame->lock);
	{
		if (op_ret == -1)
			local->op_errno = op_errno;

		if (op_ret == 0)
			local->op_ret = 0;
	}
	UNLOCK (&frame->lock);

	this_call_cnt = dht_frame_return (frame);
	if (is_last_call (this_call_cnt))
		DHT_STACK_UNWIND (frame, local->op_ret, local->op_errno);

        return 0;
}


int
dht_fsyncdir (call_frame_t *frame, xlator_t *this, fd_t *fd, int datasync)
{
	dht_local_t  *local  = NULL;
	dht_conf_t   *conf = NULL;
        int           op_errno = -1;
	int           i = -1;


        VALIDATE_OR_GOTO (frame, err);
        VALIDATE_OR_GOTO (this, err);
        VALIDATE_OR_GOTO (fd, err);

	conf = this->private;

	local = dht_local_init (frame);
	if (!local) {
		op_errno = ENOMEM;
		gf_log (this->name, GF_LOG_ERROR,
			"memory allocation failed :(");
		goto err;
	}

	local->fd = fd_ref (fd);
	local->call_cnt = conf->subvolume_cnt;

	for (i = 0; i < conf->subvolume_cnt; i++) {
		STACK_WIND (frame, dht_fsyncdir_cbk,
			    conf->subvolumes[i],
			    conf->subvolumes[i]->fops->fsyncdir,
			    fd, datasync);
	}

	return 0;

err:
	op_errno = (op_errno == -1) ? errno : op_errno;
	DHT_STACK_UNWIND (frame, -1, op_errno);

	return 0;
}


int
dht_newfile_cbk (call_frame_t *frame, void *cookie, xlator_t *this,
		 int op_ret, int op_errno,
		 inode_t *inode, struct stat *stbuf)
{
	call_frame_t *prev = NULL;
	dht_layout_t *layout = NULL;
	int           ret = -1;


	if (op_ret == -1)
		goto out;

	prev = cookie;

	dht_itransform (this, prev->this, stbuf->st_ino, &stbuf->st_ino);
	layout = dht_layout_for_subvol (this, prev->this);

	if (!layout) {
		gf_log (this->name, GF_LOG_ERROR,
			"no pre-set layout for subvolume %s",
			prev->this->name);
		op_ret   = -1;
		op_errno = EINVAL;
		goto out;
	}

	ret = inode_ctx_set (inode, this, layout);
	if (ret != 0) {
		gf_log (this->name, GF_LOG_ERROR,
			"could not set inode context");
		op_ret   = -1;
		op_errno = EINVAL;
		goto out;
	}

out:
	DHT_STACK_UNWIND (frame, op_ret, op_errno, inode, stbuf);
	return 0;
}


int
dht_mknod (call_frame_t *frame, xlator_t *this,
	   loc_t *loc, mode_t mode, dev_t rdev)
{
	xlator_t  *subvol = NULL;
	int        op_errno = -1;


	VALIDATE_OR_GOTO (frame, err);
	VALIDATE_OR_GOTO (this, err);
	VALIDATE_OR_GOTO (loc, err);

	subvol = dht_subvol_get_hashed (this, loc);
	if (!subvol) {
		gf_log (this->name, GF_LOG_ERROR,
			"no subvolume in layout for path=%s",
			loc->path);
		op_errno = ENOENT;
		goto err;
	}

	gf_log (this->name, GF_LOG_DEBUG,
		"creating %s on %s", loc->path, subvol->name);

	STACK_WIND (frame, dht_newfile_cbk,
		    subvol, subvol->fops->mknod,
		    loc, mode, rdev);

	return 0;

err:
	op_errno = (op_errno == -1) ? errno : op_errno;
	DHT_STACK_UNWIND (frame, -1, op_errno, NULL, NULL);

	return 0;
}


int
dht_symlink (call_frame_t *frame, xlator_t *this,
	     const char *linkname, loc_t *loc)
{
	xlator_t  *subvol = NULL;
	int        op_errno = -1;


	VALIDATE_OR_GOTO (frame, err);
	VALIDATE_OR_GOTO (this, err);
	VALIDATE_OR_GOTO (loc, err);

	subvol = dht_subvol_get_hashed (this, loc);
	if (!subvol) {
		gf_log (this->name, GF_LOG_ERROR,
			"no subvolume in layout for path=%s",
			loc->path);
		op_errno = ENOENT;
		goto err;
	}

	gf_log (this->name, GF_LOG_DEBUG,
		"creating %s on %s", loc->path, subvol->name);

	STACK_WIND (frame, dht_newfile_cbk,
		    subvol, subvol->fops->symlink,
		    linkname, loc);

	return 0;

err:
	op_errno = (op_errno == -1) ? errno : op_errno;
	DHT_STACK_UNWIND (frame, -1, op_errno, NULL, NULL);

	return 0;
}


int
dht_unlink (call_frame_t *frame, xlator_t *this, loc_t *loc)
{
	xlator_t    *cached_subvol = NULL;
	xlator_t    *hashed_subvol = NULL;
	int          op_errno = -1;
	dht_local_t *local = NULL;


	VALIDATE_OR_GOTO (frame, err);
	VALIDATE_OR_GOTO (this, err);
	VALIDATE_OR_GOTO (loc, err);

	cached_subvol = dht_subvol_get_cached (this, loc->inode);
	if (!cached_subvol) {
		gf_log (this->name, GF_LOG_ERROR,
			"no cached subvolume for path=%s", loc->path);
		op_errno = EINVAL;
		goto err;
	}

	hashed_subvol = dht_subvol_get_hashed (this, loc);
	if (!hashed_subvol) {
		gf_log (this->name, GF_LOG_ERROR,
			"no subvolume in layout for path=%s",
			loc->path);
		op_errno = EINVAL;
		goto err;
	}

	local = dht_local_init (frame);
	if (!local) {
		op_errno = ENOMEM;
		gf_log (this->name, GF_LOG_ERROR,
			"memory allocation failed :(");
		goto err;
	}

	local->call_cnt = 1;
	if (hashed_subvol != cached_subvol)
		local->call_cnt++;

	STACK_WIND (frame, dht_err_cbk,
		    cached_subvol, cached_subvol->fops->unlink, loc);

	if (hashed_subvol != cached_subvol)
		STACK_WIND (frame, dht_err_cbk,
			    hashed_subvol, hashed_subvol->fops->unlink, loc);

	return 0;

err:
	op_errno = (op_errno == -1) ? errno : op_errno;
	DHT_STACK_UNWIND (frame, -1, op_errno);

	return 0;
}


int
dht_link_cbk (call_frame_t *frame, void *cookie, xlator_t *this,
	      int op_ret, int op_errno,
	      inode_t *inode, struct stat *stbuf)
{
        call_frame_t *prev = NULL;
	dht_layout_t *layout = NULL;
	dht_local_t  *local = NULL;

        prev = cookie;
	local = frame->local;

        if (op_ret == -1)
                goto out;

	layout = dht_layout_for_subvol (this, prev->this);
	if (!layout) {
		gf_log (this->name, GF_LOG_ERROR,
			"no pre-set layout for subvolume %s",
			prev->this->name);
		op_ret   = -1;
		op_errno = EINVAL;
		goto out;
	}

	stbuf->st_ino = local->loc.inode->ino;

out:
        DHT_STACK_UNWIND (frame, op_ret, op_errno, inode, stbuf);

	return 0;
}


int
dht_link_linkfile_cbk (call_frame_t *frame, void *cookie, xlator_t *this,
		       int op_ret, int op_errno,
		       inode_t *inode, struct stat *stbuf)
{
	dht_local_t  *local = NULL;
	xlator_t     *srcvol = NULL;


	if (op_ret == -1)
		goto err;

	local = frame->local;
	srcvol = local->linkfile.srcvol;

	STACK_WIND (frame, dht_link_cbk,
		    srcvol, srcvol->fops->link,
		    &local->loc, &local->loc2);

	return 0;

err:
	DHT_STACK_UNWIND (frame, op_ret, op_errno, inode, stbuf);

	return 0;
}


int
dht_link (call_frame_t *frame, xlator_t *this,
	  loc_t *oldloc, loc_t *newloc)
{
	xlator_t    *cached_subvol = NULL;
	xlator_t    *hashed_subvol = NULL;
	int          op_errno = -1;
	int          ret = -1;
	dht_local_t *local = NULL;


	VALIDATE_OR_GOTO (frame, err);
	VALIDATE_OR_GOTO (this, err);
	VALIDATE_OR_GOTO (oldloc, err);
	VALIDATE_OR_GOTO (newloc, err);

	cached_subvol = dht_subvol_get_cached (this, oldloc->inode);
	if (!cached_subvol) {
		gf_log (this->name, GF_LOG_ERROR,
			"no cached subvolume for path=%s", oldloc->path);
		op_errno = EINVAL;
		goto err;
	}

	hashed_subvol = dht_subvol_get_hashed (this, newloc);
	if (!hashed_subvol) {
		gf_log (this->name, GF_LOG_ERROR,
			"no subvolume in layout for path=%s",
			newloc->path);
		op_errno = EINVAL;
		goto err;
	}

	local = dht_local_init (frame);
	if (!local) {
		op_errno = ENOMEM;
		gf_log (this->name, GF_LOG_ERROR,
			"memory allocation failed :(");
		goto err;
	}

	ret = loc_copy (&local->loc, oldloc);
	if (ret == -1) {
		op_errno = ENOMEM;
		gf_log (this->name, GF_LOG_ERROR,
			"memory allocation failed :(");
		goto err;
	}

	ret = loc_copy (&local->loc2, newloc);
	if (ret == -1) {
		op_errno = ENOMEM;
		gf_log (this->name, GF_LOG_ERROR,
			"memory allocation failed :(");
		goto err;
	}

	if (hashed_subvol != cached_subvol) {
		dht_linkfile_create (frame, dht_link_linkfile_cbk,
				     cached_subvol, hashed_subvol, newloc);
	} else {
		STACK_WIND (frame, dht_link_cbk,
			    cached_subvol, cached_subvol->fops->link,
			    oldloc, newloc);
	}

	return 0;

err:
	op_errno = (op_errno == -1) ? errno : op_errno;
	DHT_STACK_UNWIND (frame, -1, op_errno, NULL, NULL);

	return 0;
}


int
dht_create_cbk (call_frame_t *frame, void *cookie, xlator_t *this,
		 int op_ret, int op_errno,
		 fd_t *fd, inode_t *inode, struct stat *stbuf)
{
	call_frame_t *prev = NULL;
	dht_layout_t *layout = NULL;
	int           ret = -1;


	if (op_ret == -1)
		goto out;

	prev = cookie;

	dht_itransform (this, prev->this, stbuf->st_ino, &stbuf->st_ino);
	layout = dht_layout_for_subvol (this, prev->this);

	if (!layout) {
		gf_log (this->name, GF_LOG_ERROR,
			"no pre-set layout for subvolume %s",
			prev->this->name);
		op_ret   = -1;
		op_errno = EINVAL;
		goto out;
	}

	ret = inode_ctx_set (inode, this, layout);
	if (ret != 0) {
		gf_log (this->name, GF_LOG_ERROR,
			"could not set inode context");
		op_ret   = -1;
		op_errno = EINVAL;
		goto out;
	}

out:
	DHT_STACK_UNWIND (frame, op_ret, op_errno, fd, inode, stbuf);
	return 0;
}


int
dht_create (call_frame_t *frame, xlator_t *this,
	    loc_t *loc, int32_t flags, mode_t mode, fd_t *fd)
{
	xlator_t  *subvol = NULL;
	int        op_errno = -1;


	VALIDATE_OR_GOTO (frame, err);
	VALIDATE_OR_GOTO (this, err);
	VALIDATE_OR_GOTO (loc, err);

	subvol = dht_subvol_get_hashed (this, loc);
	if (!subvol) {
		gf_log (this->name, GF_LOG_ERROR,
			"no subvolume in layout for path=%s",
			loc->path);
		op_errno = ENOENT;
		goto err;
	}

	gf_log (this->name, GF_LOG_DEBUG,
		"creating %s on %s", loc->path, subvol->name);

	STACK_WIND (frame, dht_create_cbk,
		    subvol, subvol->fops->create,
		    loc, flags, mode, fd);

	return 0;

err:
	op_errno = (op_errno == -1) ? errno : op_errno;
	DHT_STACK_UNWIND (frame, -1, op_errno, NULL, NULL, NULL);

	return 0;
}


int
dht_mkdir_selfheal_cbk (call_frame_t *frame, void *cookie,
			xlator_t *this,
			int32_t op_ret, int32_t op_errno)
{
	dht_local_t   *local = NULL;
	dht_layout_t  *layout = NULL;


	local = frame->local;
	layout = local->selfheal.layout;

	if (op_ret == 0) {
		inode_ctx_set (local->inode, this, layout);
		local->selfheal.layout = NULL;
	}

	DHT_STACK_UNWIND (frame, op_ret, op_errno,
			  local->inode, &local->stbuf);

	return 0;
}


int
dht_mkdir_cbk (call_frame_t *frame, void *cookie, xlator_t *this,
	       int op_ret, int op_errno, inode_t *inode, struct stat *stbuf)
{
	dht_local_t  *local = NULL;
	int           this_call_cnt = 0;
	int           ret = -1;
	call_frame_t *prev = NULL;
	dht_layout_t *layout = NULL;

	local = frame->local;
	prev  = cookie;
	layout = local->layout;

	LOCK (&frame->lock);
	{
		ret = dht_layout_merge (this, layout, prev->this,
					op_ret, op_errno, NULL);

		if (op_ret == -1) {
			local->op_errno = op_errno;
			goto unlock;
		}
		local->op_ret = 0;

		dht_stat_merge (this, &local->stbuf, stbuf, prev->this);
	}
unlock:
	UNLOCK (&frame->lock);


	this_call_cnt = dht_frame_return (frame);
	if (is_last_call (this_call_cnt)) {
		local->layout = NULL;
		dht_selfheal_directory (frame, dht_mkdir_selfheal_cbk,
					&local->loc, layout);
	}

        return 0;
}


int
dht_mkdir (call_frame_t *frame, xlator_t *this,
	   loc_t *loc, mode_t mode)
{
	dht_local_t  *local  = NULL;
	dht_conf_t   *conf = NULL;
        int           op_errno = -1;
	int           i = -1;
	int           ret = -1;


        VALIDATE_OR_GOTO (frame, err);
        VALIDATE_OR_GOTO (this, err);
        VALIDATE_OR_GOTO (loc, err);
        VALIDATE_OR_GOTO (loc->inode, err);
        VALIDATE_OR_GOTO (loc->path, err);

	conf = this->private;

	local = dht_local_init (frame);
	if (!local) {
		gf_log (this->name, GF_LOG_ERROR,
			"memory allocation failed :(");
		op_errno = ENOMEM;
		goto err;
	}

	local->call_cnt = conf->subvolume_cnt;

	local->inode = inode_ref (loc->inode);

	ret = loc_copy (&local->loc, loc);
	if (ret == -1) {
		gf_log (this->name, GF_LOG_ERROR,
			"memory allocation failed :(");
		op_errno = ENOMEM;
		goto err;
	}

	local->layout = dht_layout_new (this, conf->subvolume_cnt);
	if (!local->layout) {
		gf_log (this->name, GF_LOG_ERROR,
			"memory allocation failed :(");
		op_errno = ENOMEM;
		goto err;
	}

	for (i = 0; i < conf->subvolume_cnt; i++) {
		STACK_WIND (frame, dht_mkdir_cbk,
			    conf->subvolumes[i],
			    conf->subvolumes[i]->fops->mkdir,
			    loc, mode);
	}

	return 0;

err:
	op_errno = (op_errno == -1) ? errno : op_errno;
	DHT_STACK_UNWIND (frame, -1, op_errno, NULL, NULL);

	return 0;
}


int
dht_rmdir_selfheal_cbk (call_frame_t *frame, void *cookie, xlator_t *this,
			int op_ret, int op_errno)
{
	dht_local_t  *local = NULL;

	local = frame->local;
	local->layout = NULL;

	DHT_STACK_UNWIND (frame, local->op_ret, local->op_errno);

	return 0;
}


int
dht_rmdir_cbk (call_frame_t *frame, void *cookie, xlator_t *this,
	       int op_ret, int op_errno)
{
	dht_local_t  *local = NULL;
	int           this_call_cnt = 0;
	call_frame_t *prev = NULL;
	dht_layout_t *layout = NULL;
	void         *tmp_layout = NULL; /* This is required to remove 'type-punned' warnings from gcc */

	local = frame->local;
	prev  = cookie;

	LOCK (&frame->lock);
	{
		if (op_ret == -1) {
			local->op_errno = op_errno;
			local->op_ret   = -1;

			if (op_errno != ENOENT)
				local->need_selfheal = 1;

			gf_log (this->name, GF_LOG_ERROR,
				"rmdir on %s for %s failed (%s)",
				prev->this->name, local->loc.path,
				strerror (op_errno));
			goto unlock;
		}
	}
unlock:
	UNLOCK (&frame->lock);


	this_call_cnt = dht_frame_return (frame);
	if (is_last_call (this_call_cnt)) {
		if (local->need_selfheal) {
			inode_ctx_get (local->loc.inode, this, &tmp_layout);
			layout = tmp_layout;

			/* TODO: neater interface needed below */
			local->stbuf.st_mode = local->loc.inode->st_mode;

			dht_selfheal_restore (frame, dht_rmdir_selfheal_cbk,
					      &local->loc, layout);
		} else {
			DHT_STACK_UNWIND (frame, local->op_ret,
					  local->op_errno);
		}
	}

        return 0;
}


int
dht_rmdir_do (call_frame_t *frame, xlator_t *this)
{
	dht_local_t  *local = NULL;
	dht_conf_t   *conf = NULL;
	int           i = 0;

	conf = this->private;
	local = frame->local;

	if (local->op_ret == -1)
		goto err;

	local->call_cnt = conf->subvolume_cnt;

	for (i = 0; i < conf->subvolume_cnt; i++) {
		STACK_WIND (frame, dht_rmdir_cbk,
			    conf->subvolumes[i],
			    conf->subvolumes[i]->fops->rmdir,
			    &local->loc);
	}

	return 0;

err:
	DHT_STACK_UNWIND (frame, local->op_ret, local->op_errno);
	return 0;
}


int
dht_rmdir_readdir_cbk (call_frame_t *frame, void *cookie, xlator_t *this,
		       int op_ret, int op_errno, gf_dirent_t *entries)
{
	dht_local_t  *local = NULL;
	int           this_call_cnt = -1;
	call_frame_t *prev = NULL;

	local = frame->local;
	prev  = cookie;

	if (op_ret > 2) {
		gf_log (this->name, GF_LOG_DEBUG,
			"readdir on %s for %s returned %d entries",
			prev->this->name, local->loc.path, op_ret);
		local->op_ret = -1;
		local->op_errno = ENOTEMPTY;
	}

	this_call_cnt = dht_frame_return (frame);

	if (is_last_call (this_call_cnt)) {
		dht_rmdir_do (frame, this);
	}

	return 0;
}


int
dht_rmdir_opendir_cbk (call_frame_t *frame, void *cookie, xlator_t *this,
		       int op_ret, int op_errno, fd_t *fd)
{
	dht_local_t  *local = NULL;
	int           this_call_cnt = -1;
	call_frame_t *prev = NULL;


	local = frame->local;
	prev  = cookie;

	if (op_ret == -1) {
		gf_log (this->name, GF_LOG_ERROR,
			"opendir on %s for %s failed (%s)",
			prev->this->name, local->loc.path,
			strerror (op_errno));
		goto err;
	}

	STACK_WIND (frame, dht_rmdir_readdir_cbk,
		    prev->this, prev->this->fops->readdir,
		    local->fd, 4096, 0);

	return 0;

err:
	this_call_cnt = dht_frame_return (frame);

	if (is_last_call (this_call_cnt)) {
		dht_rmdir_do (frame, this);
	}

	return 0;
}


int
dht_rmdir (call_frame_t *frame, xlator_t *this, loc_t *loc)
{
	dht_local_t  *local  = NULL;
	dht_conf_t   *conf = NULL;
        int           op_errno = -1;
	int           i = -1;
	int           ret = -1;


        VALIDATE_OR_GOTO (frame, err);
        VALIDATE_OR_GOTO (this, err);
        VALIDATE_OR_GOTO (loc, err);
        VALIDATE_OR_GOTO (loc->inode, err);
        VALIDATE_OR_GOTO (loc->path, err);

	conf = this->private;

	local = dht_local_init (frame);
	if (!local) {
		gf_log (this->name, GF_LOG_ERROR,
			"memory allocation failed :(");
		op_errno = ENOMEM;
		goto err;
	}

	local->call_cnt = conf->subvolume_cnt;
	local->op_ret   = 0;

	ret = loc_copy (&local->loc, loc);
	if (ret == -1) {
		gf_log (this->name, GF_LOG_ERROR,
			"memory allocation failed :(");
		op_errno = ENOMEM;
		goto err;
	}

	local->fd = fd_create (local->loc.inode, frame->root->pid);
	if (!local->fd) {
		gf_log (this->name, GF_LOG_ERROR,
			"memory allocation failed :(");
		op_errno = ENOMEM;
		goto err;
	}

	for (i = 0; i < conf->subvolume_cnt; i++) {
		STACK_WIND (frame, dht_rmdir_opendir_cbk,
			    conf->subvolumes[i],
			    conf->subvolumes[i]->fops->opendir,
			    loc, local->fd);
	}

	return 0;

err:
	op_errno = (op_errno == -1) ? errno : op_errno;
	DHT_STACK_UNWIND (frame, -1, op_errno);

	return 0;
}


static int32_t
dht_xattrop_cbk (call_frame_t *frame,
		 void *cookie,
		 xlator_t *this,
		 int32_t op_ret,
		 int32_t op_errno,
		 dict_t *dict)
{
	DHT_STACK_UNWIND (frame, op_ret, op_errno, dict);
	return 0;
}

int32_t
dht_xattrop (call_frame_t *frame,
	     xlator_t *this,
	     loc_t *loc,
	     gf_xattrop_flags_t flags,
	     dict_t *dict)
{
	xlator_t     *subvol = NULL;
        int           op_errno = -1;
	dht_local_t  *local = NULL;

        VALIDATE_OR_GOTO (frame, err);
        VALIDATE_OR_GOTO (this, err);
        VALIDATE_OR_GOTO (loc, err);
        VALIDATE_OR_GOTO (loc->inode, err);
        VALIDATE_OR_GOTO (loc->path, err);

	subvol = dht_subvol_get_cached (this, loc->inode);
	if (!subvol) {
		gf_log (this->name, GF_LOG_ERROR,
			"no cached subvolume for path=%s", loc->path);
		op_errno = EINVAL;
		goto err;
	}

	local = dht_local_init (frame);
	if (!local) {
		op_errno = ENOMEM;
		gf_log (this->name, GF_LOG_ERROR,
			"memory allocation failed :(");
		goto err;
	}

	local->inode = inode_ref (loc->inode);
	local->call_cnt = 1;

	STACK_WIND (frame,
		    dht_xattrop_cbk,
		    subvol, subvol->fops->xattrop,
		    loc, flags, dict);

	return 0;

err:
	op_errno = (op_errno == -1) ? errno : op_errno;
	DHT_STACK_UNWIND (frame, -1, op_errno, NULL);

	return 0;
}

static int32_t
dht_fxattrop_cbk (call_frame_t *frame,
		  void *cookie,
		  xlator_t *this,
		  int32_t op_ret,
		  int32_t op_errno,
		  dict_t *dict)
{
	DHT_STACK_UNWIND (frame, op_ret, op_errno, dict);
	return 0;
}

int32_t
dht_fxattrop (call_frame_t *frame,
	      xlator_t *this,
	      fd_t *fd,
	      gf_xattrop_flags_t flags,
	      dict_t *dict)
{
	xlator_t     *subvol = NULL;
        int           op_errno = -1;

        VALIDATE_OR_GOTO (frame, err);
        VALIDATE_OR_GOTO (this, err);
        VALIDATE_OR_GOTO (fd, err);

	subvol = dht_subvol_get_cached (this, fd->inode);
	if (!subvol) {
		gf_log (this->name, GF_LOG_ERROR,
			"no cached subvolume for fd=%p", fd);
		op_errno = EINVAL;
		goto err;
	}

	STACK_WIND (frame,
		    dht_fxattrop_cbk,
		    subvol, subvol->fops->fxattrop,
		    fd, flags, dict);

	return 0;

err:
	op_errno = (op_errno == -1) ? errno : op_errno;
	DHT_STACK_UNWIND (frame, -1, op_errno, NULL);

	return 0;
}


static int32_t
dht_inodelk_cbk (call_frame_t *frame, void *cookie,
		 xlator_t *this, int32_t op_ret, int32_t op_errno)

{
	DHT_STACK_UNWIND (frame, op_ret, op_errno);
	return 0;
}


int32_t
dht_inodelk (call_frame_t *frame, xlator_t *this,
	     loc_t *loc, int32_t cmd, struct flock *lock)
{
	xlator_t     *subvol = NULL;
        int           op_errno = -1;
	dht_local_t  *local = NULL;


        VALIDATE_OR_GOTO (frame, err);
        VALIDATE_OR_GOTO (this, err);
        VALIDATE_OR_GOTO (loc, err);
        VALIDATE_OR_GOTO (loc->inode, err);
        VALIDATE_OR_GOTO (loc->path, err);

	subvol = dht_subvol_get_cached (this, loc->inode);
	if (!subvol) {
		gf_log (this->name, GF_LOG_ERROR,
			"no cached subvolume for path=%s", loc->path);
		op_errno = EINVAL;
		goto err;
	}

	local = dht_local_init (frame);
	if (!local) {
		op_errno = ENOMEM;
		gf_log (this->name, GF_LOG_ERROR,
			"memory allocation failed :(");
		goto err;
	}

	local->inode = inode_ref (loc->inode);
	local->call_cnt = 1;

	STACK_WIND (frame,
		    dht_inodelk_cbk,
		    subvol, subvol->fops->inodelk,
		    loc, cmd, lock);

	return 0;

err:
	op_errno = (op_errno == -1) ? errno : op_errno;
	DHT_STACK_UNWIND (frame, -1, op_errno);

	return 0;
}


static int32_t
dht_finodelk_cbk (call_frame_t *frame, void *cookie,
		  xlator_t *this, int32_t op_ret, int32_t op_errno)

{
	DHT_STACK_UNWIND (frame, op_ret, op_errno);
	return 0;
}


int32_t
dht_finodelk (call_frame_t *frame, xlator_t *this,
	      fd_t *fd, int32_t cmd, struct flock *lock)
{
	xlator_t     *subvol = NULL;
        int           op_errno = -1;

        VALIDATE_OR_GOTO (frame, err);
        VALIDATE_OR_GOTO (this, err);
        VALIDATE_OR_GOTO (fd, err);

	subvol = dht_subvol_get_cached (this, fd->inode);
	if (!subvol) {
		gf_log (this->name, GF_LOG_ERROR,
			"no cached subvolume for fd=%p", fd);
		op_errno = EINVAL;
		goto err;
	}


	STACK_WIND (frame,
		    dht_finodelk_cbk,
		    subvol, subvol->fops->finodelk,
		    fd, cmd, lock);

	return 0;

err:
	op_errno = (op_errno == -1) ? errno : op_errno;
	DHT_STACK_UNWIND (frame, -1, op_errno);

	return 0;
}


static int32_t
dht_entrylk_cbk (call_frame_t *frame, void *cookie,
		 xlator_t *this, int32_t op_ret, int32_t op_errno)

{
	DHT_STACK_UNWIND (frame, op_ret, op_errno);
	return 0;
}

int32_t
dht_entrylk (call_frame_t *frame, xlator_t *this,
	     loc_t *loc, const char *basename,
	     entrylk_cmd cmd, entrylk_type type)
{
	xlator_t     *subvol = NULL;
        int           op_errno = -1;
	dht_local_t  *local = NULL;

        VALIDATE_OR_GOTO (frame, err);
        VALIDATE_OR_GOTO (this, err);
        VALIDATE_OR_GOTO (loc, err);
        VALIDATE_OR_GOTO (loc->inode, err);
        VALIDATE_OR_GOTO (loc->path, err);

	subvol = dht_subvol_get_cached (this, loc->inode);
	if (!subvol) {
		gf_log (this->name, GF_LOG_ERROR,
			"no cached subvolume for path=%s", loc->path);
		op_errno = EINVAL;
		goto err;
	}

	local = dht_local_init (frame);
	if (!local) {
		op_errno = ENOMEM;
		gf_log (this->name, GF_LOG_ERROR,
			"memory allocation failed :(");
		goto err;
	}

	local->inode = inode_ref (loc->inode);
	local->call_cnt = 1;

	STACK_WIND (frame, dht_entrylk_cbk,
		    subvol, subvol->fops->entrylk,
		    loc, basename, cmd, type);

	return 0;

err:
	op_errno = (op_errno == -1) ? errno : op_errno;
	DHT_STACK_UNWIND (frame, -1, op_errno);

	return 0;
}

static int32_t
dht_fentrylk_cbk (call_frame_t *frame, void *cookie,
		  xlator_t *this, int32_t op_ret, int32_t op_errno)

{
	DHT_STACK_UNWIND (frame, op_ret, op_errno);
	return 0;
}

int32_t
dht_fentrylk (call_frame_t *frame, xlator_t *this,
	      fd_t *fd, const char *basename,
	      entrylk_cmd cmd, entrylk_type type)
{
	xlator_t     *subvol = NULL;
        int           op_errno = -1;

        VALIDATE_OR_GOTO (frame, err);
        VALIDATE_OR_GOTO (this, err);
        VALIDATE_OR_GOTO (fd, err);

	subvol = dht_subvol_get_cached (this, fd->inode);
	if (!subvol) {
		gf_log (this->name, GF_LOG_ERROR,
			"no cached subvolume for fd=%p", fd);
		op_errno = EINVAL;
		goto err;
	}

	STACK_WIND (frame, dht_fentrylk_cbk,
		    subvol, subvol->fops->fentrylk,
		    fd, basename, cmd, type);

	return 0;

err:
	op_errno = (op_errno == -1) ? errno : op_errno;
	DHT_STACK_UNWIND (frame, -1, op_errno);

	return 0;
}


int
dht_forget (xlator_t *this, inode_t *inode)
{
	dht_layout_t *layout = NULL;
	void         *tmp_layout = NULL; /* This is required to remove 'type-punned' warnings from gcc */

	inode_ctx_get (inode, this, &tmp_layout);

	if (!tmp_layout)
		return 0;

	layout = tmp_layout;
	if (!layout->preset)
		FREE (layout);

	return 0;
}


static int
dht_init_subvolumes (xlator_t *this, dht_conf_t *conf)
{
        xlator_list_t *subvols = NULL;
        int            cnt = 0;


        for (subvols = this->children; subvols; subvols = subvols->next)
                cnt++;

        conf->subvolumes = calloc (cnt, sizeof (xlator_t *));
        if (!conf->subvolumes) {
                gf_log (this->name, GF_LOG_ERROR,
                        "memory allocation failed :(");
                return -1;
        }
        conf->subvolume_cnt = cnt;

        cnt = 0;
        for (subvols = this->children; subvols; subvols = subvols->next)
                conf->subvolumes[cnt++] = subvols->xlator;

	conf->subvolume_status = calloc (cnt, sizeof (char));
	if (!conf->subvolume_status) {
		gf_log (this->name, GF_LOG_ERROR,
			"memory allocation failed :(");
		return -1;
	}

        return 0;
}


int
dht_notify (xlator_t *this, int event, void *data, ...)
{
	xlator_t   *subvol = NULL;
	int         cnt    = -1;
	int         i      = -1;
	dht_conf_t *conf   = NULL;
	int         ret    = -1;


	conf = this->private;

	switch (event) {
	case GF_EVENT_CHILD_UP:
		subvol = data;

		for (i = 0; i < conf->subvolume_cnt; i++) {
			if (subvol == conf->subvolumes[i]) {
				cnt = i;
				break;
			}
		}

		if (cnt == -1) {
			gf_log (this->name, GF_LOG_ERROR,
				"got GF_EVENT_CHILD_UP bad subvolume %s",
				subvol->name);
			break;
		}

		LOCK (&conf->subvolume_lock);
		{
			conf->subvolume_status[cnt] = 1;
		}
		UNLOCK (&conf->subvolume_lock);

		break;

	case GF_EVENT_CHILD_DOWN:
		subvol = data;

		for (i = 0; i < conf->subvolume_cnt; i++) {
			if (subvol == conf->subvolumes[i]) {
				cnt = i;
				break;
			}
		}

		if (cnt == -1) {
			gf_log (this->name, GF_LOG_ERROR,
				"got GF_EVENT_CHILD_DOWN bad subvolume %s",
				subvol->name);
			break;
		}

		LOCK (&conf->subvolume_lock);
		{
			conf->subvolume_status[cnt] = 0;
		}
		UNLOCK (&conf->subvolume_lock);

		break;
	}

	ret = default_notify (this, event, data);

	return ret;
}


int
notify (xlator_t *this, int event, void *data, ...)
{
	int ret = -1;

	ret = dht_notify (this, event, data);

	return ret;
}


int
init (xlator_t *this)
{
        dht_conf_t    *conf = NULL;
	char          *lookup_unhashed_str = NULL;
        int            ret = -1;
        int            i = 0;

	if (!this->children) {
		gf_log (this->name, GF_LOG_ERROR,
			"DHT needs more than one child defined");
		return -1;
	}
  
	if (!this->parents) {
		gf_log (this->name, GF_LOG_WARNING,
			"dangling volume. check volfile ");
	}

        conf = calloc (1, sizeof (*conf));
        if (!conf) {
                gf_log (this->name, GF_LOG_ERROR,
                        "memory allocation failed :(");
                goto err;
        }

	conf->search_unhashed = 0;

	if (dict_get_str (this->options, "lookup-unhashed",
			  &lookup_unhashed_str) == 0) {
		gf_string2boolean (lookup_unhashed_str,
				   (gf_boolean_t *)&conf->search_unhashed);
	}

        ret = dht_init_subvolumes (this, conf);
        if (ret == -1) {
                goto err;
        }

        ret = dht_layouts_init (this, conf);
        if (ret == -1) {
                goto err;
        }

	LOCK_INIT (&conf->subvolume_lock);

        this->private = conf;

        return 0;

err:
        if (conf) {
                if (conf->file_layouts) {
                        for (i = 0; i < conf->subvolume_cnt; i++) {
                                FREE (conf->file_layouts[i]);
                        }
                        FREE (conf->file_layouts);
                }

                if (conf->default_dir_layout)
                        FREE (conf->default_dir_layout);

                if (conf->subvolumes)
                        FREE (conf->subvolumes);

		if (conf->subvolume_status)
			FREE (conf->subvolume_status);

                FREE (conf);
        }

        return -1;
}


void
fini (xlator_t *this)
{

}


struct xlator_fops fops = {
	.lookup      = dht_lookup,
	.stat        = dht_stat,
	.chmod       = dht_chmod,
	.chown       = dht_chown,
	.fchown      = dht_fchown,
	.fchmod      = dht_fchmod,
	.fstat       = dht_fstat,
	.utimens     = dht_utimens,
	.truncate    = dht_truncate,
	.ftruncate   = dht_ftruncate,
	.access      = dht_access,
	.readlink    = dht_readlink,
	.setxattr    = dht_setxattr,
	.getxattr    = dht_getxattr,
	.removexattr = dht_removexattr,
	.open        = dht_open,
	.readv       = dht_readv,
	.writev      = dht_writev,
	.flush       = dht_flush,
	.fsync       = dht_fsync,
	.statfs      = dht_statfs,
	.lk          = dht_lk,
	.opendir     = dht_opendir,
	.readdir     = dht_readdir,
	.fsyncdir    = dht_fsyncdir,
	.mknod       = dht_mknod,
	.symlink     = dht_symlink,
	.unlink      = dht_unlink,
	.link        = dht_link,
	.create      = dht_create,
	.mkdir       = dht_mkdir,
	.rmdir       = dht_rmdir,
	.rename      = dht_rename,
	.inodelk     = dht_inodelk,
	.finodelk    = dht_finodelk,
	.entrylk     = dht_entrylk,
	.fentrylk    = dht_fentrylk,
	.xattrop     = dht_xattrop,
	.fxattrop    = dht_fxattrop,
#if 0
	.setdents    = dht_setdents,
	.getdents    = dht_getdents,
	.checksum    = dht_checksum,
#endif
};


struct xlator_mops mops = {
};


struct xlator_cbks cbks = {
//	.release    = dht_release,
//      .releasedir = dht_releasedir,
	.forget     = dht_forget
};


struct xlator_options options[] = {
        { "lookup-unhashed", GF_OPTION_TYPE_BOOL, 0, 0, 0 },
        { NULL, 0, 0, 0, 0 },
};
