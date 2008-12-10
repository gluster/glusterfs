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
#include "nufa.h"
#include "defaults.h"


/* TODO:
   - use volumename in xattr instead of "nufa"
   - use NS locks
   - handle all cases in self heal layout reconstruction
   - complete linkfile selfheal
*/


int
nufa_lookup_selfheal_cbk (call_frame_t *frame, void *cookie,
			 xlator_t *this,
			 int op_ret, int op_errno)
{
	nufa_local_t  *local = NULL;
	nufa_layout_t *layout = NULL;
	int           ret = 0;

	local = frame->local;
	ret = op_ret;

	if (ret == 0) {
		layout = local->selfheal.layout;
		ret = inode_ctx_set (local->inode, this, layout);

		if (ret == 0)
			local->selfheal.layout = NULL;
	}

	NUFA_STACK_UNWIND (frame, ret, local->op_errno, local->inode,
			  &local->stbuf, local->xattr);

	return 0;
}


int
nufa_lookup_dir_cbk (call_frame_t *frame, void *cookie, xlator_t *this,
                    int op_ret, int op_errno,
                    inode_t *inode, struct stat *stbuf, dict_t *xattr)
{
        nufa_local_t  *local         = NULL;
        int           this_call_cnt = 0;
        call_frame_t *prev          = NULL;
	nufa_layout_t *layout        = NULL;
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

		ret = nufa_layout_merge (this, layout, prev->this,
					op_ret, op_errno, xattr);

		if (op_ret == -1) {
			gf_log (this->name, GF_LOG_WARNING,
				"lookup of %s on %s returned error (%s)",
				local->loc.path, prev->this->name,
				strerror (op_errno));
			
			goto unlock;
		}

		nufa_stat_merge (this, &local->stbuf, stbuf, prev->this);
        }
unlock:
        UNLOCK (&frame->lock);


        this_call_cnt = nufa_frame_return (frame);

        if (is_last_call (this_call_cnt)) {
		if (local->op_ret == 0) {
			ret = nufa_layout_normalize (this, &local->loc,layout);

			local->layout = NULL;

			if (ret != 0) {
				gf_log (this->name, GF_LOG_WARNING,
					"fixing assignment on %s",
					local->loc.path);
				goto selfheal;
			}

			inode_ctx_set (local->inode, this, layout);
		}

		NUFA_STACK_UNWIND (frame, local->op_ret, local->op_errno,
				  local->inode, &local->stbuf, local->xattr);
        }

	return 0;

selfheal:
	ret = nufa_selfheal_directory (frame, nufa_lookup_selfheal_cbk,
				       &local->loc, layout);

	return 0;
}

int
nufa_lookup_linkfile_cbk (call_frame_t *frame, void *cookie,
                         xlator_t *this, int op_ret, int op_errno,
                         inode_t *inode, struct stat *stbuf, dict_t *xattr)
{
        call_frame_t *prev = NULL;
	nufa_layout_t *layout = NULL;

        prev = cookie;
        /* TODO: assert type is non-dir and non-linkfile */

        if (op_ret == -1)
                goto out;

        nufa_itransform (this, prev->this, stbuf->st_ino, &stbuf->st_ino);

	layout = nufa_layout_for_subvol (this, prev->this);
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
        NUFA_STACK_UNWIND (frame, op_ret, op_errno, inode, stbuf, xattr);

        return 0;
}


int
nufa_revalidate_cbk (call_frame_t *frame, void *cookie, xlator_t *this,
                    int op_ret, int op_errno,
                    inode_t *inode, struct stat *stbuf, dict_t *xattr)
{
        nufa_local_t  *local         = NULL;
        int           this_call_cnt = 0;
        call_frame_t *prev          = NULL;
	nufa_layout_t *layout        = NULL;


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


		nufa_stat_merge (this, &local->stbuf, stbuf, prev->this);


		local->op_ret = 0;
		local->stbuf.st_ino = local->st_ino;

		if (!local->xattr)
			local->xattr = dict_ref (xattr);
		
	}
unlock:
	UNLOCK (&frame->lock);

        this_call_cnt = nufa_frame_return (frame);

        if (is_last_call (this_call_cnt)) {
		NUFA_STACK_UNWIND (frame, local->op_ret, local->op_errno,
				   local->inode, &local->stbuf, local->xattr);
	}

        return 0;
}


int
nufa_lookup_cbk (call_frame_t *frame, void *cookie, xlator_t *this,
                int op_ret, int op_errno,
                inode_t *inode, struct stat *stbuf, dict_t *xattr)
{
	nufa_layout_t *layout      = NULL;
        char          is_linkfile = 0;
        char          is_dir      = 0;
        xlator_t     *subvol      = NULL;
        nufa_conf_t   *conf        = NULL;
        nufa_local_t  *local       = NULL;
        loc_t        *loc         = NULL;
        int           i           = 0;
        call_frame_t *prev        = NULL;
	int           call_cnt    = 0;


        conf  = this->private;

        prev  = cookie;
        local = frame->local;
        loc   = &local->loc;

        if (op_ret == -1)
                goto out;

        is_linkfile = check_is_linkfile (inode, stbuf, xattr);
        is_dir      = check_is_dir (inode, stbuf, xattr);

        if (!is_dir && !is_linkfile) {
                /* non-directory and not a linkfile */

		nufa_itransform (this, prev->this, stbuf->st_ino,
				&stbuf->st_ino);

		layout = nufa_layout_for_subvol (this, prev->this);
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

		local->layout = nufa_layout_new (this, conf->subvolume_cnt);
		if (!local->layout) {
			op_ret   = -1;
			op_errno = ENOMEM;
			gf_log (this->name, GF_LOG_ERROR,
				"memory allocation failed :(");
			goto out;
		}

                for (i = 0; i < call_cnt; i++) {
                        STACK_WIND (frame, nufa_lookup_dir_cbk,
                                    conf->subvolumes[i],
                                    conf->subvolumes[i]->fops->lookup,
                                    &local->loc, 1);
                }
        }

        if (is_linkfile) {
                subvol = nufa_linkfile_subvol (this, inode, stbuf, xattr);

                if (!subvol) {
                        gf_log (this->name, GF_LOG_WARNING,
                                "linkfile not having link subvolume. path=%s",
                                loc->path);
                        op_ret   = -1;
                        op_errno = EINVAL;
                        goto out;
                }

                STACK_WIND (frame, nufa_lookup_linkfile_cbk,
                            subvol, subvol->fops->lookup,
                            &local->loc, 1);
        }

        return 0;

out:
        NUFA_STACK_UNWIND (frame, op_ret, op_errno, inode, stbuf, xattr);
        return 0;
}

int 
nufa_local_lookup_cbk (call_frame_t *frame, void *cookie, xlator_t *this,
		      int op_ret, int op_errno,
		      inode_t *inode, struct stat *stbuf, dict_t *xattr)
{
        nufa_local_t  *local  = frame->local;
        call_frame_t *prev   = NULL;
        xlator_t     *subvol = NULL;
        xlator_t     *hashed_subvol = NULL;
        xlator_t     *cached_subvol = NULL;
	nufa_layout_t *layout = NULL;
        nufa_conf_t   *conf   = NULL;
	int           i = 0;
	int           call_cnt = 0;
        char          is_linkfile = 0;
        char          is_dir      = 0;
	
	prev = cookie;
	conf  = this->private;
        local = frame->local;

	hashed_subvol = nufa_subvol_get_hashed (this, &local->loc);
	cached_subvol = nufa_subvol_get_cached (this, local->loc.inode);
	
	local->cached_subvol = cached_subvol;
	local->hashed_subvol = hashed_subvol;

        if (op_ret == -1)
                goto out;

	if (op_ret >= 0) {

		is_linkfile = check_is_linkfile (inode, stbuf, xattr);
		is_dir      = check_is_dir (inode, stbuf, xattr);

		
		if (!is_dir && !is_linkfile) {
			/* non-directory and not a linkfile */

			nufa_itransform (this, prev->this, stbuf->st_ino,
					&stbuf->st_ino);
		
			layout = nufa_layout_for_subvol (this, prev->this);
			if (!layout) {
				gf_log (this->name, GF_LOG_ERROR,
					"no pre-set layout for subvolume %s",
					prev->this->name);
				op_ret   = -1;
				op_errno = EINVAL;
				goto err;
			}

			inode_ctx_set (inode, this, layout);
			goto err;
		}
	
		if (is_dir) {
			call_cnt        = conf->subvolume_cnt;
			local->call_cnt = call_cnt;
			
			local->inode = inode_ref (inode);
			local->xattr = dict_ref (xattr);

			local->op_ret = 0;
			local->op_errno = 0;

			local->layout = nufa_layout_new (this, 
							 conf->subvolume_cnt);
			if (!local->layout) {
				op_ret   = -1;
				op_errno = ENOMEM;
				gf_log (this->name, GF_LOG_ERROR,
					"memory allocation failed :(");
				goto err;
			}

			for (i = 0; i < call_cnt; i++) {
				STACK_WIND (frame, nufa_lookup_dir_cbk,
					    conf->subvolumes[i],
					    conf->subvolumes[i]->fops->lookup,
					    &local->loc, 1);
			}
		}
		
		if (is_linkfile) {
			subvol = nufa_linkfile_subvol (this, inode, 
						       stbuf, xattr);
			
			if (!subvol) {
				gf_log (this->name, GF_LOG_ERROR,
					"linkfile not having link subvolume. "
					"path=%s", local->loc.path);
				op_ret   = -1;
				op_errno = EINVAL;
				goto err;
			}
			
			STACK_WIND (frame, nufa_lookup_linkfile_cbk,
				    subvol, subvol->fops->lookup,
				    &local->loc, 1);
		}
	
		return 0;
	}

 out:
	if (!hashed_subvol) {
		gf_log (this->name, GF_LOG_ERROR,
			"no subvolume in layout for path=%s",
			local->loc.path);
		op_errno = EINVAL;
		NUFA_STACK_UNWIND (frame, op_ret, op_errno, 
				   inode, stbuf, xattr);
		return 0;
	}
		
	STACK_WIND (frame, nufa_lookup_cbk,
		    hashed_subvol, hashed_subvol->fops->lookup,
		    &local->loc, 1);

	return 0;

 err:
        NUFA_STACK_UNWIND (frame, op_ret, op_errno, inode, stbuf, xattr);
	return 0;
	
}

int
nufa_lookup (call_frame_t *frame, xlator_t *this,
            loc_t *loc, int need_xattr)
{
        xlator_t     *subvol = NULL;
        nufa_local_t  *local  = NULL;
        int           ret    = -1;
        int           op_errno = -1;
	nufa_layout_t *layout = NULL;
        nufa_conf_t   *conf   = NULL;
	int           i = 0;
	int           call_cnt = 0;

        VALIDATE_OR_GOTO (frame, err);
        VALIDATE_OR_GOTO (this, err);
        VALIDATE_OR_GOTO (loc, err);
        VALIDATE_OR_GOTO (loc->inode, err);
        VALIDATE_OR_GOTO (loc->path, err);

        conf  = this->private;

        local = nufa_local_init (frame);
	if (!local) {
		op_errno = ENOMEM;
		gf_log (this->name, GF_LOG_ERROR,
			"memory allocation failed :(");
		goto err;
	}

        ret = loc_copy (&local->loc, loc);
        if (ret == -1) {
                op_errno = errno;
                gf_log (this->name, GF_LOG_ERROR,
                        "copying location failed for path=%s",
                        loc->path);
                goto err;
        }

	if (is_revalidate (loc)) {
		layout = nufa_layout_get (this, loc->inode);
		
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

			STACK_WIND (frame, nufa_revalidate_cbk,
				    subvol, subvol->fops->lookup,
				    loc, need_xattr);
			
			if (!--call_cnt)
				break;
		}
	} else {
		/* Send it to only local volume */
		STACK_WIND (frame, nufa_local_lookup_cbk,
			    conf->local_volume, conf->local_volume->fops->lookup,
			    loc, 1);
	}

        return 0;

err:
	op_errno = (op_errno == -1) ? errno : op_errno;
        NUFA_STACK_UNWIND (frame, -1, op_errno, NULL, NULL, NULL);
        return 0;
}


int
nufa_attr_cbk (call_frame_t *frame, void *cookie, xlator_t *this,
	      int op_ret, int op_errno, struct stat *stbuf)
{
	nufa_local_t  *local = NULL;
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

		nufa_stat_merge (this, &local->stbuf, stbuf, prev->this);
		
		if (local->inode)
			local->stbuf.st_ino = local->inode->ino;
		local->op_ret = 0;
	}
unlock:
	UNLOCK (&frame->lock);

	this_call_cnt = nufa_frame_return (frame);
	if (is_last_call (this_call_cnt))
		NUFA_STACK_UNWIND (frame, local->op_ret, local->op_errno,
				  &local->stbuf);

        return 0;
}


int
nufa_stat (call_frame_t *frame, xlator_t *this,
	  loc_t *loc)
{
	xlator_t     *subvol = NULL;
        int           op_errno = -1;
	nufa_local_t  *local = NULL;
	nufa_layout_t *layout = NULL;
	int           i = 0;


        VALIDATE_OR_GOTO (frame, err);
        VALIDATE_OR_GOTO (this, err);
        VALIDATE_OR_GOTO (loc, err);
        VALIDATE_OR_GOTO (loc->inode, err);
        VALIDATE_OR_GOTO (loc->path, err);

	layout = nufa_layout_get (this, loc->inode);
	if (!layout) {
		gf_log (this->name, GF_LOG_ERROR,
			"no layout for path=%s", loc->path);
		op_errno = EINVAL;
		goto err;
	}

	local = nufa_local_init (frame);
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

		STACK_WIND (frame, nufa_attr_cbk,
			    subvol, subvol->fops->stat,
			    loc);
	}

	return 0;

err:
	op_errno = (op_errno == -1) ? errno : op_errno;
	NUFA_STACK_UNWIND (frame, -1, op_errno, NULL);

	return 0;
}


int
nufa_fstat (call_frame_t *frame, xlator_t *this,
	   fd_t *fd)
{
	xlator_t     *subvol = NULL;
        int           op_errno = -1;
	nufa_local_t  *local = NULL;
	nufa_layout_t *layout = NULL;
	int           i = 0;


        VALIDATE_OR_GOTO (frame, err);
        VALIDATE_OR_GOTO (this, err);
        VALIDATE_OR_GOTO (fd, err);

	layout = nufa_layout_get (this, fd->inode);
	if (!layout) {
		gf_log (this->name, GF_LOG_ERROR,
			"no layout for fd=%p", fd);
		op_errno = EINVAL;
		goto err;
	}

	local = nufa_local_init (frame);
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
		STACK_WIND (frame, nufa_attr_cbk,
			    subvol, subvol->fops->fstat,
			    fd);
	}

	return 0;

err:
	op_errno = (op_errno == -1) ? errno : op_errno;
	NUFA_STACK_UNWIND (frame, -1, op_errno, NULL);

	return 0;
}


int
nufa_chmod (call_frame_t *frame, xlator_t *this,
	   loc_t *loc, mode_t mode)
{
	nufa_layout_t *layout = NULL;
	nufa_local_t  *local  = NULL;
        int           op_errno = -1;
	int           i = -1;


        VALIDATE_OR_GOTO (frame, err);
        VALIDATE_OR_GOTO (this, err);
        VALIDATE_OR_GOTO (loc, err);
        VALIDATE_OR_GOTO (loc->inode, err);
        VALIDATE_OR_GOTO (loc->path, err);

	layout = nufa_layout_get (this, loc->inode);

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

	local = nufa_local_init (frame);
	if (!local) {
		op_errno = ENOMEM;
		gf_log (this->name, GF_LOG_ERROR,
			"memory allocation failed :(");
		goto err;
	}

	local->inode = inode_ref (loc->inode);
	local->call_cnt = layout->cnt;

	for (i = 0; i < layout->cnt; i++) {
		STACK_WIND (frame, nufa_attr_cbk,
			    layout->list[i].xlator,
			    layout->list[i].xlator->fops->chmod,
			    loc, mode);
	}

	return 0;

err:
	op_errno = (op_errno == -1) ? errno : op_errno;
	NUFA_STACK_UNWIND (frame, -1, op_errno, NULL);

	return 0;
}


int
nufa_chown (call_frame_t *frame, xlator_t *this,
	   loc_t *loc, uid_t uid, gid_t gid)
{
	nufa_layout_t *layout = NULL;
	nufa_local_t  *local  = NULL;
        int           op_errno = -1;
	int           i = -1;


        VALIDATE_OR_GOTO (frame, err);
        VALIDATE_OR_GOTO (this, err);
        VALIDATE_OR_GOTO (loc, err);
        VALIDATE_OR_GOTO (loc->inode, err);
        VALIDATE_OR_GOTO (loc->path, err);

	layout = nufa_layout_get (this, loc->inode);
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

	local = nufa_local_init (frame);
	if (!local) {
		op_errno = ENOMEM;
		gf_log (this->name, GF_LOG_ERROR,
			"memory allocation failed :(");
		goto err;
	}

	local->inode = inode_ref (loc->inode);
	local->call_cnt = layout->cnt;

	for (i = 0; i < layout->cnt; i++) {
		STACK_WIND (frame, nufa_attr_cbk,
			    layout->list[i].xlator,
			    layout->list[i].xlator->fops->chown,
			    loc, uid, gid);
	}

	return 0;

err:
	op_errno = (op_errno == -1) ? errno : op_errno;
	NUFA_STACK_UNWIND (frame, -1, op_errno, NULL);

	return 0;
}


int
nufa_fchmod (call_frame_t *frame, xlator_t *this,
	    fd_t *fd, mode_t mode)
{
	nufa_layout_t *layout = NULL;
	nufa_local_t  *local  = NULL;
        int           op_errno = -1;
	int           i = -1;


        VALIDATE_OR_GOTO (frame, err);
        VALIDATE_OR_GOTO (this, err);
        VALIDATE_OR_GOTO (fd, err);


	layout = nufa_layout_get (this, fd->inode);
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

	local = nufa_local_init (frame);
	if (!local) {
		op_errno = ENOMEM;
		gf_log (this->name, GF_LOG_ERROR,
			"memory allocation failed :(");
		goto err;
	}

	local->inode = inode_ref (fd->inode);
	local->call_cnt = layout->cnt;

	for (i = 0; i < layout->cnt; i++) {
		STACK_WIND (frame, nufa_attr_cbk,
			    layout->list[i].xlator,
			    layout->list[i].xlator->fops->fchmod,
			    fd, mode);
	}

	return 0;

err:
	op_errno = (op_errno == -1) ? errno : op_errno;
	NUFA_STACK_UNWIND (frame, -1, op_errno, NULL);

	return 0;
}


int
nufa_fchown (call_frame_t *frame, xlator_t *this,
	    fd_t *fd, uid_t uid, gid_t gid)
{
	nufa_layout_t *layout = NULL;
	nufa_local_t  *local  = NULL;
        int           op_errno = -1;
	int           i = -1;


        VALIDATE_OR_GOTO (frame, err);
        VALIDATE_OR_GOTO (this, err);
        VALIDATE_OR_GOTO (fd, err);

	layout = nufa_layout_get (this, fd->inode);
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

	local = nufa_local_init (frame);
	if (!local) {
		op_errno = ENOMEM;
		gf_log (this->name, GF_LOG_ERROR,
			"memory allocation failed :(");
		goto err;
	}

	local->inode = inode_ref (fd->inode);
	local->call_cnt = layout->cnt;

	for (i = 0; i < layout->cnt; i++) {
		STACK_WIND (frame, nufa_attr_cbk,
			    layout->list[i].xlator,
			    layout->list[i].xlator->fops->fchown,
			    fd, uid, gid);
	}

	return 0;

err:
	op_errno = (op_errno == -1) ? errno : op_errno;
	NUFA_STACK_UNWIND (frame, -1, op_errno, NULL);

	return 0;
}


int
nufa_utimens (call_frame_t *frame, xlator_t *this,
	     loc_t *loc, struct timespec tv[2])
{
	nufa_layout_t *layout = NULL;
	nufa_local_t  *local  = NULL;
        int           op_errno = -1;
	int           i = -1;


        VALIDATE_OR_GOTO (frame, err);
        VALIDATE_OR_GOTO (this, err);
        VALIDATE_OR_GOTO (loc, err);
        VALIDATE_OR_GOTO (loc->inode, err);
        VALIDATE_OR_GOTO (loc->path, err);

	layout = nufa_layout_get (this, loc->inode);
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

	local = nufa_local_init (frame);
	if (!local) {
		op_errno = ENOMEM;
		gf_log (this->name, GF_LOG_ERROR,
			"memory allocation failed :(");
		goto err;
	}

	local->inode = inode_ref (loc->inode);
	local->call_cnt = layout->cnt;

	for (i = 0; i < layout->cnt; i++) {
		STACK_WIND (frame, nufa_attr_cbk,
			    layout->list[i].xlator,
			    layout->list[i].xlator->fops->utimens,
			    loc, tv);
	}

	return 0;

err:
	op_errno = (op_errno == -1) ? errno : op_errno;
	NUFA_STACK_UNWIND (frame, -1, op_errno, NULL);

	return 0;
}


int
nufa_truncate (call_frame_t *frame, xlator_t *this,
	      loc_t *loc, off_t offset)
{
	xlator_t     *subvol = NULL;
        int           op_errno = -1;
	nufa_local_t  *local = NULL;


        VALIDATE_OR_GOTO (frame, err);
        VALIDATE_OR_GOTO (this, err);
        VALIDATE_OR_GOTO (loc, err);
        VALIDATE_OR_GOTO (loc->inode, err);
        VALIDATE_OR_GOTO (loc->path, err);

	subvol = nufa_subvol_get_cached (this, loc->inode);
	if (!subvol) {
		gf_log (this->name, GF_LOG_ERROR,
			"no cached subvolume for path=%s", loc->path);
		op_errno = EINVAL;
		goto err;
	}

	local = nufa_local_init (frame);
	if (!local) {
		op_errno = ENOMEM;
		gf_log (this->name, GF_LOG_ERROR,
			"memory allocation failed :(");
		goto err;
	}

	local->inode = inode_ref (loc->inode);
	local->call_cnt = 1;

	STACK_WIND (frame, nufa_attr_cbk,
		    subvol, subvol->fops->truncate,
		    loc, offset);

	return 0;

err:
	op_errno = (op_errno == -1) ? errno : op_errno;
	NUFA_STACK_UNWIND (frame, -1, op_errno, NULL);

	return 0;
}


int
nufa_ftruncate (call_frame_t *frame, xlator_t *this,
	       fd_t *fd, off_t offset)
{
	xlator_t     *subvol = NULL;
        int           op_errno = -1;
	nufa_local_t  *local = NULL;


        VALIDATE_OR_GOTO (frame, err);
        VALIDATE_OR_GOTO (this, err);
        VALIDATE_OR_GOTO (fd, err);

	subvol = nufa_subvol_get_cached (this, fd->inode);
	if (!subvol) {
		gf_log (this->name, GF_LOG_ERROR,
			"no cached subvolume for fd=%p", fd);
		op_errno = EINVAL;
		goto err;
	}

	local = nufa_local_init (frame);
	if (!local) {
		op_errno = ENOMEM;
		gf_log (this->name, GF_LOG_ERROR,
			"memory allocation failed :(");
		goto err;
	}

	local->inode = inode_ref (fd->inode);
	local->call_cnt = 1;

	STACK_WIND (frame, nufa_attr_cbk,
		    subvol, subvol->fops->ftruncate,
		    fd, offset);

	return 0;

err:
	op_errno = (op_errno == -1) ? errno : op_errno;
	NUFA_STACK_UNWIND (frame, -1, op_errno, NULL);

	return 0;
}


int
nufa_err_cbk (call_frame_t *frame, void *cookie, xlator_t *this,
	     int op_ret, int op_errno)
{
	nufa_local_t  *local = NULL;
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

	this_call_cnt = nufa_frame_return (frame);
	if (is_last_call (this_call_cnt))
		NUFA_STACK_UNWIND (frame, local->op_ret, local->op_errno);

        return 0;
}


int
nufa_access (call_frame_t *frame, xlator_t *this,
	    loc_t *loc, int32_t mask)
{
	xlator_t     *subvol = NULL;
        int           op_errno = -1;
	nufa_local_t  *local = NULL;


        VALIDATE_OR_GOTO (frame, err);
        VALIDATE_OR_GOTO (this, err);
        VALIDATE_OR_GOTO (loc, err);
        VALIDATE_OR_GOTO (loc->inode, err);
        VALIDATE_OR_GOTO (loc->path, err);

	subvol = nufa_subvol_get_cached (this, loc->inode);
	if (!subvol) {
		gf_log (this->name, GF_LOG_ERROR,
			"no cached subvolume for path=%s", loc->path);
		op_errno = EINVAL;
		goto err;
	}

	local = nufa_local_init (frame);
	if (!local) {
		op_errno = ENOMEM;
		gf_log (this->name, GF_LOG_ERROR,
			"memory allocation failed :(");
		goto err;
	}

	local->call_cnt = 1;

	STACK_WIND (frame, nufa_err_cbk,
		    subvol, subvol->fops->access,
		    loc, mask);

	return 0;

err:
	op_errno = (op_errno == -1) ? errno : op_errno;
	NUFA_STACK_UNWIND (frame, -1, op_errno);

	return 0;
}


int
nufa_readlink_cbk (call_frame_t *frame, void *cookie, xlator_t *this,
		  int op_ret, int op_errno, const char *path)
{
        NUFA_STACK_UNWIND (frame, op_ret, op_errno, path);

        return 0;
}


int
nufa_readlink (call_frame_t *frame, xlator_t *this,
	      loc_t *loc, size_t size)
{
	xlator_t     *subvol = NULL;
        int           op_errno = -1;


        VALIDATE_OR_GOTO (frame, err);
        VALIDATE_OR_GOTO (this, err);
        VALIDATE_OR_GOTO (loc, err);
        VALIDATE_OR_GOTO (loc->inode, err);
        VALIDATE_OR_GOTO (loc->path, err);

	subvol = nufa_subvol_get_cached (this, loc->inode);
	if (!subvol) {
		gf_log (this->name, GF_LOG_ERROR,
			"no cached subvolume for path=%s", loc->path);
		op_errno = EINVAL;
		goto err;
	}

	STACK_WIND (frame, nufa_readlink_cbk,
		    subvol, subvol->fops->readlink,
		    loc, size);

	return 0;

err:
	op_errno = (op_errno == -1) ? errno : op_errno;
	NUFA_STACK_UNWIND (frame, -1, op_errno, NULL);

	return 0;
}


int
nufa_getxattr_cbk (call_frame_t *frame, void *cookie, xlator_t *this,
		  int op_ret, int op_errno, dict_t *xattr)
{
        NUFA_STACK_UNWIND (frame, op_ret, op_errno, xattr);

        return 0;
}


int
nufa_getxattr (call_frame_t *frame, xlator_t *this,
	      loc_t *loc, const char *key)
{
	xlator_t     *subvol = NULL;
        int           op_errno = -1;


        VALIDATE_OR_GOTO (frame, err);
        VALIDATE_OR_GOTO (this, err);
        VALIDATE_OR_GOTO (loc, err);
        VALIDATE_OR_GOTO (loc->inode, err);
        VALIDATE_OR_GOTO (loc->path, err);

	subvol = nufa_subvol_get_cached (this, loc->inode);
	if (!subvol) {
		gf_log (this->name, GF_LOG_ERROR,
			"no cached subvolume for path=%s", loc->path);
		op_errno = EINVAL;
		goto err;
	}

	STACK_WIND (frame, nufa_getxattr_cbk,
		    subvol, subvol->fops->getxattr,
		    loc, key);

	return 0;

err:
	op_errno = (op_errno == -1) ? errno : op_errno;
	NUFA_STACK_UNWIND (frame, -1, op_errno, NULL);

	return 0;
}


int
nufa_setxattr (call_frame_t *frame, xlator_t *this,
	      loc_t *loc, dict_t *xattr, int flags)
{
	xlator_t     *subvol = NULL;
        int           op_errno = -1;
	nufa_local_t  *local = NULL;


        VALIDATE_OR_GOTO (frame, err);
        VALIDATE_OR_GOTO (this, err);
        VALIDATE_OR_GOTO (loc, err);
        VALIDATE_OR_GOTO (loc->inode, err);
        VALIDATE_OR_GOTO (loc->path, err);

	subvol = nufa_subvol_get_cached (this, loc->inode);
	if (!subvol) {
		gf_log (this->name, GF_LOG_ERROR,
			"no cached subvolume for path=%s", loc->path);
		op_errno = EINVAL;
		goto err;
	}

	local = nufa_local_init (frame);
	if (!local) {
		op_errno = ENOMEM;
		gf_log (this->name, GF_LOG_ERROR,
			"memory allocation failed :(");
		goto err;
	}

	local->call_cnt = 1;

	STACK_WIND (frame, nufa_err_cbk,
		    subvol, subvol->fops->setxattr,
		    loc, xattr, flags);

	return 0;

err:
	op_errno = (op_errno == -1) ? errno : op_errno;
	NUFA_STACK_UNWIND (frame, -1, op_errno, NULL);

	return 0;
}


int
nufa_removexattr (call_frame_t *frame, xlator_t *this,
		 loc_t *loc, const char *key)
{
	xlator_t     *subvol = NULL;
        int           op_errno = -1;
	nufa_local_t  *local = NULL;


        VALIDATE_OR_GOTO (frame, err);
        VALIDATE_OR_GOTO (this, err);
        VALIDATE_OR_GOTO (loc, err);
        VALIDATE_OR_GOTO (loc->inode, err);
        VALIDATE_OR_GOTO (loc->path, err);

	subvol = nufa_subvol_get_cached (this, loc->inode);
	if (!subvol) {
		gf_log (this->name, GF_LOG_ERROR,
			"no cached subvolume for path=%s", loc->path);
		op_errno = EINVAL;
		goto err;
	}

	local = nufa_local_init (frame);
	if (!local) {
		op_errno = ENOMEM;
		gf_log (this->name, GF_LOG_ERROR,
			"memory allocation failed :(");
		goto err;
	}

	local->call_cnt = 1;

	STACK_WIND (frame, nufa_err_cbk,
		    subvol, subvol->fops->removexattr,
		    loc, key);

	return 0;

err:
	op_errno = (op_errno == -1) ? errno : op_errno;
	NUFA_STACK_UNWIND (frame, -1, op_errno, NULL);

	return 0;
}


int
nufa_fd_cbk (call_frame_t *frame, void *cookie, xlator_t *this,
	      int op_ret, int op_errno, fd_t *fd)
{
	nufa_local_t  *local = NULL;
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

	this_call_cnt = nufa_frame_return (frame);
	if (is_last_call (this_call_cnt))
		NUFA_STACK_UNWIND (frame, local->op_ret, local->op_errno,
				  local->fd);

        return 0;
}


int
nufa_open (call_frame_t *frame, xlator_t *this,
	  loc_t *loc, int flags, fd_t *fd)
{
	xlator_t     *subvol = NULL;
	int           ret = -1;
        int           op_errno = -1;
	nufa_local_t  *local = NULL;


        VALIDATE_OR_GOTO (frame, err);
        VALIDATE_OR_GOTO (this, err);
        VALIDATE_OR_GOTO (fd, err);

	subvol = nufa_subvol_get_cached (this, fd->inode);
	if (!subvol) {
		gf_log (this->name, GF_LOG_ERROR,
			"no cached subvolume for fd=%p", fd);
		op_errno = EINVAL;
		goto err;
	}

	local = nufa_local_init (frame);
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

	STACK_WIND (frame, nufa_fd_cbk,
		    subvol, subvol->fops->open,
		    loc, flags, fd);

	return 0;

err:
	op_errno = (op_errno == -1) ? errno : op_errno;
	NUFA_STACK_UNWIND (frame, -1, op_errno, NULL);

	return 0;
}


int
nufa_readv_cbk (call_frame_t *frame, void *cookie, xlator_t *this,
	       int op_ret, int op_errno,
	       struct iovec *vector, int count, struct stat *stbuf)
{
        NUFA_STACK_UNWIND (frame, op_ret, op_errno, vector, count, stbuf);

        return 0;
}


int
nufa_readv (call_frame_t *frame, xlator_t *this,
	   fd_t *fd, size_t size, off_t off)
{
	xlator_t     *subvol = NULL;
        int           op_errno = -1;


        VALIDATE_OR_GOTO (frame, err);
        VALIDATE_OR_GOTO (this, err);
        VALIDATE_OR_GOTO (fd, err);

	subvol = nufa_subvol_get_cached (this, fd->inode);
	if (!subvol) {
		gf_log (this->name, GF_LOG_ERROR,
			"no cached subvolume for fd=%p", fd);
		op_errno = EINVAL;
		goto err;
	}

	STACK_WIND (frame, nufa_readv_cbk,
		    subvol, subvol->fops->readv,
		    fd, size, off);

	return 0;

err:
	op_errno = (op_errno == -1) ? errno : op_errno;
	NUFA_STACK_UNWIND (frame, -1, op_errno, NULL, 0, NULL);

	return 0;
}


int
nufa_writev_cbk (call_frame_t *frame, void *cookie, xlator_t *this,
		int op_ret, int op_errno, struct stat *stbuf)
{
        NUFA_STACK_UNWIND (frame, op_ret, op_errno, stbuf);

        return 0;
}


int
nufa_writev (call_frame_t *frame, xlator_t *this,
	    fd_t *fd, struct iovec *vector, int count, off_t off)
{
	xlator_t     *subvol = NULL;
        int           op_errno = -1;


        VALIDATE_OR_GOTO (frame, err);
        VALIDATE_OR_GOTO (this, err);
        VALIDATE_OR_GOTO (fd, err);

	subvol = nufa_subvol_get_cached (this, fd->inode);
	if (!subvol) {
		gf_log (this->name, GF_LOG_ERROR,
			"no cached subvolume for fd=%p", fd);
		op_errno = EINVAL;
		goto err;
	}

	STACK_WIND (frame, nufa_writev_cbk,
		    subvol, subvol->fops->writev,
		    fd, vector, count, off);

	return 0;

err:
	op_errno = (op_errno == -1) ? errno : op_errno;
	NUFA_STACK_UNWIND (frame, -1, op_errno, NULL, 0);

	return 0;
}


int
nufa_flush (call_frame_t *frame, xlator_t *this, fd_t *fd)
{
	xlator_t     *subvol = NULL;
        int           op_errno = -1;
	nufa_local_t  *local = NULL;


        VALIDATE_OR_GOTO (frame, err);
        VALIDATE_OR_GOTO (this, err);
        VALIDATE_OR_GOTO (fd, err);

	subvol = nufa_subvol_get_cached (this, fd->inode);
	if (!subvol) {
		gf_log (this->name, GF_LOG_ERROR,
			"no cached subvolume for fd=%p", fd);
		op_errno = EINVAL;
		goto err;
	}

	local = nufa_local_init (frame);
	if (!local) {
		op_errno = ENOMEM;
		gf_log (this->name, GF_LOG_ERROR,
			"memory allocation failed :(");
		goto err;
	}

	local->fd = fd_ref (fd);
	local->call_cnt = 1;

	STACK_WIND (frame, nufa_err_cbk,
		    subvol, subvol->fops->flush, fd);

	return 0;

err:
	op_errno = (op_errno == -1) ? errno : op_errno;
	NUFA_STACK_UNWIND (frame, -1, op_errno);

	return 0;
}


int
nufa_fsync (call_frame_t *frame, xlator_t *this,
	   fd_t *fd, int datasync)
{
	xlator_t     *subvol = NULL;
        int           op_errno = -1;
	nufa_local_t  *local = NULL;


        VALIDATE_OR_GOTO (frame, err);
        VALIDATE_OR_GOTO (this, err);
        VALIDATE_OR_GOTO (fd, err);

	subvol = nufa_subvol_get_cached (this, fd->inode);
	if (!subvol) {
		gf_log (this->name, GF_LOG_ERROR,
			"no cached subvolume for fd=%p", fd);
		op_errno = EINVAL;
		goto err;
	}

	local = nufa_local_init (frame);
	if (!local) {
		op_errno = ENOMEM;
		gf_log (this->name, GF_LOG_ERROR,
			"memory allocatoin failed :(");
		goto err;
	}
	local->call_cnt = 1;

	STACK_WIND (frame, nufa_err_cbk,
		    subvol, subvol->fops->fsync,
		    fd, datasync);

	return 0;

err:
	op_errno = (op_errno == -1) ? errno : op_errno;
	NUFA_STACK_UNWIND (frame, -1, op_errno);

	return 0;
}


int
nufa_lk_cbk (call_frame_t *frame, void *cookie, xlator_t *this,
	    int op_ret, int op_errno, struct flock *flock)
{
        NUFA_STACK_UNWIND (frame, op_ret, op_errno, flock);

        return 0;
}


int
nufa_lk (call_frame_t *frame, xlator_t *this,
	fd_t *fd, int cmd, struct flock *flock)
{
	xlator_t     *subvol = NULL;
        int           op_errno = -1;


        VALIDATE_OR_GOTO (frame, err);
        VALIDATE_OR_GOTO (this, err);
        VALIDATE_OR_GOTO (fd, err);

	subvol = nufa_subvol_get_cached (this, fd->inode);
	if (!subvol) {
		gf_log (this->name, GF_LOG_ERROR,
			"no cached subvolume for fd=%p", fd);
		op_errno = EINVAL;
		goto err;
	}

	STACK_WIND (frame, nufa_lk_cbk,
		    subvol, subvol->fops->lk,
		    fd, cmd, flock);

	return 0;

err:
	op_errno = (op_errno == -1) ? errno : op_errno;
	NUFA_STACK_UNWIND (frame, -1, op_errno, NULL);

	return 0;
}

/* gf_lk no longer exists 
int
nufa_gf_lk_cbk (call_frame_t *frame, void *cookie, xlator_t *this,
	    int op_ret, int op_errno, struct flock *flock)
{
        NUFA_STACK_UNWIND (frame, op_ret, op_errno, flock);

        return 0;
}


int
nufa_gf_lk (call_frame_t *frame, xlator_t *this,
	   loc_t *loc, int cmd, struct flock *flock)
{
	xlator_t     *subvol = NULL;
        int           op_errno = -1;


        VALIDATE_OR_GOTO (frame, err);
        VALIDATE_OR_GOTO (this, err);
        VALIDATE_OR_GOTO (fd, err);

	subvol = nufa_subvol_get_cached (this, fd->inode);
	if (!subvol) {
		gf_log (this->name, GF_LOG_ERROR,
			"no cached subvolume for fd=%p", fd);
		op_errno = EINVAL;
		goto err;
	}

	STACK_WIND (frame, nufa_gf_lk_cbk,
		    subvol, subvol->fops->gf_lk,
		    fd, cmd, flock);

	return 0;

err:
	op_errno = (op_errno == -1) ? errno : op_errno;
	NUFA_STACK_UNWIND (frame, -1, op_errno, NULL);

	return 0;
}
*/

int
nufa_statfs_cbk (call_frame_t *frame, void *cookie, xlator_t *this,
		int op_ret, int op_errno, struct statvfs *statvfs)
{
	nufa_local_t  *local = NULL;
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


	this_call_cnt = nufa_frame_return (frame);
	if (is_last_call (this_call_cnt))
		NUFA_STACK_UNWIND (frame, local->op_ret, local->op_errno,
				  &local->statvfs);

        return 0;
}


int
nufa_statfs (call_frame_t *frame, xlator_t *this, loc_t *loc)
{
	nufa_local_t  *local  = NULL;
	nufa_conf_t   *conf = NULL;
        int           op_errno = -1;
	int           i = -1;


        VALIDATE_OR_GOTO (frame, err);
        VALIDATE_OR_GOTO (this, err);
        VALIDATE_OR_GOTO (loc, err);
        VALIDATE_OR_GOTO (loc->inode, err);
        VALIDATE_OR_GOTO (loc->path, err);

	conf = this->private;

	local = nufa_local_init (frame);
	local->call_cnt = conf->subvolume_cnt;

	for (i = 0; i < conf->subvolume_cnt; i++) {
		STACK_WIND (frame, nufa_statfs_cbk,
			    conf->subvolumes[i],
			    conf->subvolumes[i]->fops->statfs, loc);
	}

	return 0;

err:
	op_errno = (op_errno == -1) ? errno : op_errno;
	NUFA_STACK_UNWIND (frame, -1, op_errno, NULL);

	return 0;
}


int
nufa_opendir (call_frame_t *frame, xlator_t *this, loc_t *loc, fd_t *fd)
{
	nufa_local_t  *local  = NULL;
	nufa_conf_t   *conf = NULL;
	int           ret = -1;
        int           op_errno = -1;
	int           i = -1;


        VALIDATE_OR_GOTO (frame, err);
        VALIDATE_OR_GOTO (this, err);
        VALIDATE_OR_GOTO (fd, err);

	conf = this->private;

	local = nufa_local_init (frame);
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
		STACK_WIND (frame, nufa_fd_cbk,
			    conf->subvolumes[i],
			    conf->subvolumes[i]->fops->opendir,
			    loc, fd);
	}

	return 0;

err:
	op_errno = (op_errno == -1) ? errno : op_errno;
	NUFA_STACK_UNWIND (frame, -1, op_errno, NULL);

	return 0;
}


int
nufa_readdir_cbk (call_frame_t *frame, void *cookie, xlator_t *this,
		 int op_ret, int op_errno, gf_dirent_t *orig_entries)
{
	nufa_local_t  *local = NULL;
	gf_dirent_t   entries;
	gf_dirent_t  *orig_entry = NULL;
	gf_dirent_t  *entry = NULL;
	call_frame_t *prev = NULL;
	xlator_t     *subvol = NULL;
	xlator_t     *next = NULL;
	nufa_layout_t *layout = NULL;
	int           count = 0;


	INIT_LIST_HEAD (&entries.list);
	prev = cookie;
	local = frame->local;

	if (op_ret >= 0)
		local->op_ret = 0;

	if (op_ret < 0)
		goto done;

	layout = nufa_layout_get (this, local->fd->inode);

	list_for_each_entry (orig_entry, &orig_entries->list, list) {
		subvol = nufa_layout_search (this, layout, orig_entry->d_name);

		if (!subvol || subvol == prev->this) {
			entry = gf_dirent_for_name (orig_entry->d_name);
			if (!entry) {
				gf_log (this->name, GF_LOG_ERROR,
					"memory allocation failed :(");
				goto unwind;
			}

			nufa_itransform (this, subvol, orig_entry->d_ino,
					&entry->d_ino);
			nufa_itransform (this, subvol, orig_entry->d_off,
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
		next = nufa_subvol_next (this, prev->this);
		if (!next) {
			op_ret = local->op_ret;
			goto unwind;
		}

		STACK_WIND (frame, nufa_readdir_cbk,
			    next, next->fops->readdir,
			    local->fd, local->size, 0);
		return 0;
	}

unwind:
	NUFA_STACK_UNWIND (frame, op_ret, op_errno, &entries);

	gf_dirent_free (&entries);

        return 0;
}



int
nufa_readdir (call_frame_t *frame, xlator_t *this,
	     fd_t *fd, size_t size, off_t yoff)
{
	nufa_local_t  *local  = NULL;
	nufa_conf_t   *conf = NULL;
        int           op_errno = -1;
	xlator_t     *xvol = NULL;
	off_t         xoff = 0;


        VALIDATE_OR_GOTO (frame, err);
        VALIDATE_OR_GOTO (this, err);
        VALIDATE_OR_GOTO (fd, err);

	conf = this->private;

	local = nufa_local_init (frame);
	if (!local) {
		gf_log (this->name, GF_LOG_ERROR,
			"memory allocation failed :(");
		op_errno = ENOMEM;
		goto err;
	}

	local->fd = fd_ref (fd);
	local->size = size;

	nufa_deitransform (this, yoff, &xvol, (uint64_t *)&xoff);

	/* TODO: do proper readdir */
	STACK_WIND (frame, nufa_readdir_cbk,
		    xvol, xvol->fops->readdir,
		    fd, size, xoff);

	return 0;

err:
	op_errno = (op_errno == -1) ? errno : op_errno;
	NUFA_STACK_UNWIND (frame, -1, op_errno, NULL);

	return 0;
}


int
nufa_fsyncdir_cbk (call_frame_t *frame, void *cookie, xlator_t *this,
		  int op_ret, int op_errno)
{
	nufa_local_t  *local = NULL;
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

	this_call_cnt = nufa_frame_return (frame);
	if (is_last_call (this_call_cnt))
		NUFA_STACK_UNWIND (frame, local->op_ret, local->op_errno);

        return 0;
}


int
nufa_fsyncdir (call_frame_t *frame, xlator_t *this, fd_t *fd, int datasync)
{
	nufa_local_t  *local  = NULL;
	nufa_conf_t   *conf = NULL;
        int           op_errno = -1;
	int           i = -1;


        VALIDATE_OR_GOTO (frame, err);
        VALIDATE_OR_GOTO (this, err);
        VALIDATE_OR_GOTO (fd, err);

	conf = this->private;

	local = nufa_local_init (frame);
	if (!local) {
		op_errno = ENOMEM;
		gf_log (this->name, GF_LOG_ERROR,
			"memory allocation failed :(");
		goto err;
	}

	local->fd = fd_ref (fd);
	local->call_cnt = conf->subvolume_cnt;

	for (i = 0; i < conf->subvolume_cnt; i++) {
		STACK_WIND (frame, nufa_fsyncdir_cbk,
			    conf->subvolumes[i],
			    conf->subvolumes[i]->fops->fsyncdir,
			    fd, datasync);
	}

	return 0;

err:
	op_errno = (op_errno == -1) ? errno : op_errno;
	NUFA_STACK_UNWIND (frame, -1, op_errno);

	return 0;
}


int
nufa_newfile_cbk (call_frame_t *frame, void *cookie, xlator_t *this,
		 int op_ret, int op_errno,
		 inode_t *inode, struct stat *stbuf)
{
	call_frame_t *prev = NULL;
	nufa_layout_t *layout = NULL;
	int           ret = -1;
	nufa_local_t  *local = NULL;

	if (op_ret == -1)
		goto out;

	prev  = cookie;
	local = frame->local;

	nufa_itransform (this, prev->this, stbuf->st_ino, &stbuf->st_ino);
	layout = nufa_layout_for_subvol (this, prev->this);

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
	NUFA_STACK_UNWIND (frame, op_ret, op_errno, inode, stbuf);
	return 0;
}


int
nufa_mknod_linkfile_xattr_cbk (call_frame_t *frame, void *cookie, xlator_t *this,
			      int op_ret, int op_errno)
{
	nufa_local_t *local = NULL;
        nufa_conf_t   *conf  = NULL;

	local = frame->local;
	conf = this->private;

	if (op_ret == -1) {
		gf_log (this->name, GF_LOG_CRITICAL,
			"possible inconsistency, linkfile created, extended attribute not written");
	}

	dict_unref (local->linkfile.xattr);

	STACK_WIND (frame, nufa_newfile_cbk,
		    conf->local_volume, conf->local_volume->fops->mknod,
		    &local->loc, local->mode, local->rdev);

	return 0;
}


int
nufa_mknod_linkfile_create_cbk (call_frame_t *frame, void *cookie, xlator_t *this,
			       int op_ret, int op_errno,
			       inode_t *inode, struct stat *stbuf)
{
	nufa_local_t  *local = NULL;
	call_frame_t *prev = NULL;
        nufa_conf_t   *conf  = NULL;
	dict_t       *xattr = NULL;
	int           ret = -1;

	local = frame->local;
	prev  = cookie;
	conf  = this->private;

	if (op_ret == -1)
		goto err;

	xattr = get_new_dict ();
	if (!xattr) {
		gf_log (this->name, GF_LOG_ERROR,
			"memory allocation failed :(");
		op_errno = ENOMEM;
		goto err;
	}

	local->linkfile.xattr = dict_ref (xattr);

	ret = dict_set (xattr, "trusted.glusterfs.dht.linkto", 
			str_to_data (conf->local_volume->name));
	if (ret < 0) {
		gf_log (this->name, GF_LOG_ERROR,
			"failed to initialize linkfile data");
		op_errno = EINVAL;
		goto err;
	}

	STACK_WIND (frame, nufa_mknod_linkfile_xattr_cbk,
		    prev->this, prev->this->fops->setxattr,
		    &local->loc, local->linkfile.xattr, 0);

	return 0;

err:
	NUFA_STACK_UNWIND (frame, -1, op_errno, NULL, NULL, NULL);	
	return 0;
}

int
nufa_mknod (call_frame_t *frame, xlator_t *this,
	   loc_t *loc, mode_t mode, dev_t rdev)
{
	xlator_t     *subvol = NULL;
	nufa_local_t  *local  = NULL;	
        nufa_conf_t   *conf   = NULL;
	int           op_errno = -1;
	int           ret    = -1;

	VALIDATE_OR_GOTO (frame, err);
	VALIDATE_OR_GOTO (this, err);
	VALIDATE_OR_GOTO (loc, err);

	conf = this->private;

	local = nufa_local_init (frame);
	if (!local) {
		gf_log (this->name, GF_LOG_ERROR,
			"memory allocation failed :(");
		op_errno = ENOMEM;
		goto err;
	}

	subvol = nufa_subvol_get_hashed (this, loc);
	if (!subvol) {
		gf_log (this->name, GF_LOG_ERROR,
			"no subvolume in layout for path=%s",
			loc->path);
		op_errno = ENOENT;
		goto err;
	}

	gf_log (this->name, GF_LOG_DEBUG,
		"creating %s on %s", loc->path, subvol->name);

	if (conf->local_volume != subvol) {
		/* Create linkfile first */
		ret = loc_copy (&local->loc, loc);
		if (ret == -1) {
			gf_log (this->name, GF_LOG_ERROR,
				"memory allocation failed :(");
			op_errno = ENOMEM;
			goto err;
		}

		local->mode = mode;
		local->rdev = rdev;
		
		STACK_WIND (frame, nufa_mknod_linkfile_create_cbk,
			    subvol, subvol->fops->mknod, loc,
			    S_IFREG | NUFA_LINKFILE_MODE, 0);

		return 0;
	}

	STACK_WIND (frame, nufa_newfile_cbk,
		    subvol, subvol->fops->mknod,
		    loc, mode, rdev);

	return 0;

err:
	op_errno = (op_errno == -1) ? errno : op_errno;
	NUFA_STACK_UNWIND (frame, -1, op_errno, NULL, NULL);

	return 0;
}


int
nufa_symlink (call_frame_t *frame, xlator_t *this,
	     const char *linkname, loc_t *loc)
{
	xlator_t  *subvol = NULL;
	int        op_errno = -1;


	VALIDATE_OR_GOTO (frame, err);
	VALIDATE_OR_GOTO (this, err);
	VALIDATE_OR_GOTO (loc, err);

	subvol = nufa_subvol_get_hashed (this, loc);
	if (!subvol) {
		gf_log (this->name, GF_LOG_ERROR,
			"no subvolume in layout for path=%s",
			loc->path);
		op_errno = ENOENT;
		goto err;
	}

	gf_log (this->name, GF_LOG_DEBUG,
		"creating %s on %s", loc->path, subvol->name);

	STACK_WIND (frame, nufa_newfile_cbk,
		    subvol, subvol->fops->symlink,
		    linkname, loc);

	return 0;

err:
	op_errno = (op_errno == -1) ? errno : op_errno;
	NUFA_STACK_UNWIND (frame, -1, op_errno, NULL, NULL);

	return 0;
}


int
nufa_unlink (call_frame_t *frame, xlator_t *this, loc_t *loc)
{
	xlator_t    *cached_subvol = NULL;
	xlator_t    *hashed_subvol = NULL;
	int          op_errno = -1;
	nufa_local_t *local = NULL;

	VALIDATE_OR_GOTO (frame, err);
	VALIDATE_OR_GOTO (this, err);
	VALIDATE_OR_GOTO (loc, err);

	cached_subvol = nufa_subvol_get_cached (this, loc->inode);
	if (!cached_subvol) {
		gf_log (this->name, GF_LOG_ERROR,
			"no cached subvolume for path=%s", loc->path);
		op_errno = EINVAL;
		goto err;
	}

	hashed_subvol = nufa_subvol_get_hashed (this, loc);
	if (!hashed_subvol) {
		gf_log (this->name, GF_LOG_ERROR,
			"no subvolume in layout for path=%s",
			loc->path);
		op_errno = EINVAL;
		goto err;
	}

	local = nufa_local_init (frame);
	if (!local) {
		op_errno = ENOMEM;
		gf_log (this->name, GF_LOG_ERROR,
			"memory allocation failed :(");
		goto err;
	}

	local->call_cnt = 1;
	if (hashed_subvol != cached_subvol)
		local->call_cnt++;

	STACK_WIND (frame, nufa_err_cbk,
		    cached_subvol, cached_subvol->fops->unlink, loc);

	if (hashed_subvol != cached_subvol)
		STACK_WIND (frame, nufa_err_cbk,
			    hashed_subvol, hashed_subvol->fops->unlink, loc);

	return 0;

err:
	op_errno = (op_errno == -1) ? errno : op_errno;
	NUFA_STACK_UNWIND (frame, -1, op_errno);

	return 0;
}


int
nufa_link_cbk (call_frame_t *frame, void *cookie, xlator_t *this,
	      int op_ret, int op_errno,
	      inode_t *inode, struct stat *stbuf)
{
        call_frame_t *prev = NULL;
	nufa_layout_t *layout = NULL;
	nufa_local_t  *local = NULL;

        prev = cookie;
	local = frame->local;

        if (op_ret == -1)
                goto out;

	layout = nufa_layout_for_subvol (this, prev->this);
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
        NUFA_STACK_UNWIND (frame, op_ret, op_errno, inode, stbuf);

	return 0;
}


int
nufa_link_linkfile_cbk (call_frame_t *frame, void *cookie, xlator_t *this,
		       int op_ret, int op_errno,
		       inode_t *inode, struct stat *stbuf)
{
	nufa_local_t  *local = NULL;
	xlator_t     *srcvol = NULL;


	if (op_ret == -1)
		goto err;

	local = frame->local;
	srcvol = local->linkfile.srcvol;

	STACK_WIND (frame, nufa_link_cbk,
		    srcvol, srcvol->fops->link,
		    &local->loc, &local->loc2);

	return 0;

err:
	NUFA_STACK_UNWIND (frame, op_ret, op_errno, inode, stbuf);

	return 0;
}


int
nufa_link (call_frame_t *frame, xlator_t *this,
	  loc_t *oldloc, loc_t *newloc)
{
	xlator_t    *cached_subvol = NULL;
	xlator_t    *hashed_subvol = NULL;
	int          op_errno = -1;
	int          ret = -1;
	nufa_local_t *local = NULL;


	VALIDATE_OR_GOTO (frame, err);
	VALIDATE_OR_GOTO (this, err);
	VALIDATE_OR_GOTO (oldloc, err);
	VALIDATE_OR_GOTO (newloc, err);

	cached_subvol = nufa_subvol_get_cached (this, oldloc->inode);
	if (!cached_subvol) {
		gf_log (this->name, GF_LOG_ERROR,
			"no cached subvolume for path=%s", oldloc->path);
		op_errno = EINVAL;
		goto err;
	}

	hashed_subvol = nufa_subvol_get_hashed (this, newloc);
	if (!hashed_subvol) {
		gf_log (this->name, GF_LOG_ERROR,
			"no subvolume in layout for path=%s",
			newloc->path);
		op_errno = EINVAL;
		goto err;
	}

	local = nufa_local_init (frame);
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
		nufa_linkfile_create (frame, nufa_link_linkfile_cbk,
				     cached_subvol, hashed_subvol, newloc);
	} else {
		STACK_WIND (frame, nufa_link_cbk,
			    cached_subvol, cached_subvol->fops->link,
			    oldloc, newloc);
	}

	return 0;

err:
	op_errno = (op_errno == -1) ? errno : op_errno;
	NUFA_STACK_UNWIND (frame, -1, op_errno, NULL, NULL);

	return 0;
}


int
nufa_create_cbk (call_frame_t *frame, void *cookie, xlator_t *this,
		 int op_ret, int op_errno,
		 fd_t *fd, inode_t *inode, struct stat *stbuf)
{
	nufa_local_t  *local = NULL;
	call_frame_t *prev = NULL;
	nufa_layout_t *layout = NULL;
	int           ret = -1;

	if (op_ret == -1)
		goto out;

	prev = cookie;
	local = frame->local;

	nufa_itransform (this, prev->this, stbuf->st_ino, &stbuf->st_ino);
	layout = nufa_layout_for_subvol (this, prev->this);

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
	NUFA_STACK_UNWIND (frame, op_ret, op_errno, fd, inode, stbuf);
	return 0;
}


int
nufa_create_linkfile_xattr_cbk (call_frame_t *frame, void *cookie, xlator_t *this,
			       int op_ret, int op_errno)
{
	nufa_local_t *local = NULL;
        nufa_conf_t   *conf  = NULL;

	local = frame->local;
	conf = this->private;

	if (op_ret == -1) {
		gf_log (this->name, GF_LOG_CRITICAL,
			"possible inconsistency, linkfile created, extended attribute not written");
	}

	STACK_WIND (frame, nufa_create_cbk,
		    conf->local_volume, conf->local_volume->fops->create,
		    &local->loc, local->flags, local->mode, local->fd);

	return 0;
}


int
nufa_create_linkfile_create_cbk (call_frame_t *frame, void *cookie, xlator_t *this,
				int op_ret, int op_errno,
				inode_t *inode, struct stat *stbuf)
{
	nufa_local_t  *local = NULL;
	call_frame_t *prev = NULL;
        nufa_conf_t   *conf  = NULL;
	dict_t       *xattr = NULL;
	int           ret = -1;

	local = frame->local;
	prev  = cookie;
	conf  = this->private;

	if (op_ret == -1)
		goto err;

	xattr = get_new_dict ();
	if (!xattr) {
		gf_log (this->name, GF_LOG_ERROR,
			"memory allocation failed :(");
		op_errno = ENOMEM;
		goto err;
	}

	local->linkfile.xattr = dict_ref (xattr);

	ret = dict_set (xattr, "trusted.glusterfs.dht.linkto", 
			str_to_data (conf->local_volume->name));
	if (ret < 0) {
		gf_log (this->name, GF_LOG_ERROR,
			"failed to initialize linkfile data");
		op_errno = EINVAL;
		goto err;
	}

	STACK_WIND (frame, nufa_create_linkfile_xattr_cbk,
		    prev->this, prev->this->fops->setxattr,
		    &local->loc, local->linkfile.xattr, 0);

	return 0;

err:
	NUFA_STACK_UNWIND (frame, -1, op_errno, NULL, NULL, NULL);	
	return 0;
}



int
nufa_create (call_frame_t *frame, xlator_t *this,
	    loc_t *loc, int32_t flags, mode_t mode, fd_t *fd)
{
	xlator_t  *subvol   = NULL;
        nufa_conf_t   *conf  = NULL;
	nufa_local_t *local  = NULL;
	int        op_errno = -1;
	int        ret      = -1;

	VALIDATE_OR_GOTO (frame, err);
	VALIDATE_OR_GOTO (this, err);
	VALIDATE_OR_GOTO (loc, err);

	conf = this->private;

	local = nufa_local_init (frame);
	if (!local) {
		gf_log (this->name, GF_LOG_ERROR,
			"memory allocation failed :(");
		op_errno = ENOMEM;
		goto err;
	}

	subvol = nufa_subvol_get_hashed (this, loc);
	if (!subvol) {
		gf_log (this->name, GF_LOG_ERROR,
			"no subvolume in layout for path=%s",
			loc->path);
		op_errno = ENOENT;
		goto err;
	}

	gf_log (this->name, GF_LOG_DEBUG,
		"creating %s on %s", loc->path, subvol->name);
	
	if (subvol != conf->local_volume) {
		/* create a link file instead of actual file */
		ret = loc_copy (&local->loc, loc);
		if (ret == -1) {
			gf_log (this->name, GF_LOG_ERROR,
				"memory allocation failed :(");
			op_errno = ENOMEM;
			goto err;
		}

		local->fd = fd_ref (fd);
		local->mode = mode;
		local->flags = flags;
		
		STACK_WIND (frame, nufa_create_linkfile_create_cbk,
			    subvol, subvol->fops->mknod, loc,
			    S_IFREG | NUFA_LINKFILE_MODE, 0);

		return 0;
	}

	STACK_WIND (frame, nufa_create_cbk,
		    subvol, subvol->fops->create,
		    loc, flags, mode, fd);

	return 0;

err:
	op_errno = (op_errno == -1) ? errno : op_errno;
	NUFA_STACK_UNWIND (frame, -1, op_errno, NULL, NULL, NULL);

	return 0;
}


int
nufa_mkdir_selfheal_cbk (call_frame_t *frame, void *cookie,
			xlator_t *this,
			int32_t op_ret, int32_t op_errno)
{
	nufa_local_t   *local = NULL;
	nufa_layout_t  *layout = NULL;


	local = frame->local;
	layout = local->selfheal.layout;

	if (op_ret == 0) {
		inode_ctx_set (local->inode, this, layout);
		local->selfheal.layout = NULL;
	}

	NUFA_STACK_UNWIND (frame, op_ret, op_errno,
			  local->inode, &local->stbuf);

	return 0;
}


int
nufa_mkdir_cbk (call_frame_t *frame, void *cookie, xlator_t *this,
	       int op_ret, int op_errno, inode_t *inode, struct stat *stbuf)
{
	nufa_local_t  *local = NULL;
	int           this_call_cnt = 0;
	int           ret = -1;
	call_frame_t *prev = NULL;
	nufa_layout_t *layout = NULL;

	local = frame->local;
	prev  = cookie;
	layout = local->layout;

	LOCK (&frame->lock);
	{
		ret = nufa_layout_merge (this, layout, prev->this,
					op_ret, op_errno, NULL);

		if (op_ret == -1) {
			local->op_errno = op_errno;
			goto unlock;
		}
		local->op_ret = 0;

		nufa_stat_merge (this, &local->stbuf, stbuf, prev->this);
	}
unlock:
	UNLOCK (&frame->lock);


	this_call_cnt = nufa_frame_return (frame);
	if (is_last_call (this_call_cnt)) {
		local->layout = NULL;
		nufa_selfheal_directory (frame, nufa_mkdir_selfheal_cbk,
					&local->loc, layout);
	}

        return 0;
}


int
nufa_mkdir (call_frame_t *frame, xlator_t *this,
	   loc_t *loc, mode_t mode)
{
	nufa_local_t  *local  = NULL;
	nufa_conf_t   *conf = NULL;
        int           op_errno = -1;
	int           i = -1;
	int           ret = -1;


        VALIDATE_OR_GOTO (frame, err);
        VALIDATE_OR_GOTO (this, err);
        VALIDATE_OR_GOTO (loc, err);
        VALIDATE_OR_GOTO (loc->inode, err);
        VALIDATE_OR_GOTO (loc->path, err);

	conf = this->private;

	local = nufa_local_init (frame);
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

	local->layout = nufa_layout_new (this, conf->subvolume_cnt);
	if (!local->layout) {
		gf_log (this->name, GF_LOG_ERROR,
			"memory allocation failed :(");
		op_errno = ENOMEM;
		goto err;
	}

	for (i = 0; i < conf->subvolume_cnt; i++) {
		STACK_WIND (frame, nufa_mkdir_cbk,
			    conf->subvolumes[i],
			    conf->subvolumes[i]->fops->mkdir,
			    loc, mode);
	}

	return 0;

err:
	op_errno = (op_errno == -1) ? errno : op_errno;
	NUFA_STACK_UNWIND (frame, -1, op_errno, NULL, NULL);

	return 0;
}


int
nufa_rmdir_selfheal_cbk (call_frame_t *frame, void *cookie, xlator_t *this,
			int op_ret, int op_errno)
{
	nufa_local_t  *local = NULL;

	local = frame->local;
	local->layout = NULL;

	NUFA_STACK_UNWIND (frame, local->op_ret, local->op_errno);

	return 0;
}


int
nufa_rmdir_cbk (call_frame_t *frame, void *cookie, xlator_t *this,
	       int op_ret, int op_errno)
{
	nufa_local_t  *local = NULL;
	int           this_call_cnt = 0;
	call_frame_t *prev = NULL;
	nufa_layout_t *layout = NULL;
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


	this_call_cnt = nufa_frame_return (frame);
	if (is_last_call (this_call_cnt)) {
		if (local->need_selfheal) {
			inode_ctx_get (local->loc.inode, this, &tmp_layout);
			layout = tmp_layout;

			/* TODO: neater interface needed below */
			local->stbuf.st_mode = local->loc.inode->st_mode;

			nufa_selfheal_restore (frame, nufa_rmdir_selfheal_cbk,
					      &local->loc, layout);
		} else {
			NUFA_STACK_UNWIND (frame, local->op_ret,
					  local->op_errno);
		}
	}

        return 0;
}


int
nufa_rmdir_do (call_frame_t *frame, xlator_t *this)
{
	nufa_local_t  *local = NULL;
	nufa_conf_t   *conf = NULL;
	int           i = 0;

	conf = this->private;
	local = frame->local;

	if (local->op_ret == -1)
		goto err;

	local->call_cnt = conf->subvolume_cnt;

	for (i = 0; i < conf->subvolume_cnt; i++) {
		STACK_WIND (frame, nufa_rmdir_cbk,
			    conf->subvolumes[i],
			    conf->subvolumes[i]->fops->rmdir,
			    &local->loc);
	}

	return 0;

err:
	NUFA_STACK_UNWIND (frame, local->op_ret, local->op_errno);
	return 0;
}


int
nufa_rmdir_readdir_cbk (call_frame_t *frame, void *cookie, xlator_t *this,
		       int op_ret, int op_errno, gf_dirent_t *entries)
{
	nufa_local_t  *local = NULL;
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

	this_call_cnt = nufa_frame_return (frame);

	if (is_last_call (this_call_cnt)) {
		nufa_rmdir_do (frame, this);
	}

	return 0;
}


int
nufa_rmdir_opendir_cbk (call_frame_t *frame, void *cookie, xlator_t *this,
		       int op_ret, int op_errno, fd_t *fd)
{
	nufa_local_t  *local = NULL;
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

	STACK_WIND (frame, nufa_rmdir_readdir_cbk,
		    prev->this, prev->this->fops->readdir,
		    local->fd, 4096, 0);

	return 0;

err:
	this_call_cnt = nufa_frame_return (frame);

	if (is_last_call (this_call_cnt)) {
		nufa_rmdir_do (frame, this);
	}

	return 0;
}


int
nufa_rmdir (call_frame_t *frame, xlator_t *this, loc_t *loc)
{
	nufa_local_t  *local  = NULL;
	nufa_conf_t   *conf = NULL;
        int           op_errno = -1;
	int           i = -1;
	int           ret = -1;


        VALIDATE_OR_GOTO (frame, err);
        VALIDATE_OR_GOTO (this, err);
        VALIDATE_OR_GOTO (loc, err);
        VALIDATE_OR_GOTO (loc->inode, err);
        VALIDATE_OR_GOTO (loc->path, err);

	conf = this->private;

	local = nufa_local_init (frame);
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
		STACK_WIND (frame, nufa_rmdir_opendir_cbk,
			    conf->subvolumes[i],
			    conf->subvolumes[i]->fops->opendir,
			    loc, local->fd);
	}

	return 0;

err:
	op_errno = (op_errno == -1) ? errno : op_errno;
	NUFA_STACK_UNWIND (frame, -1, op_errno);

	return 0;
}


static int32_t
nufa_xattrop_cbk (call_frame_t *frame,
		 void *cookie,
		 xlator_t *this,
		 int32_t op_ret,
		 int32_t op_errno,
		 dict_t *dict)
{
	NUFA_STACK_UNWIND (frame, op_ret, op_errno, dict);
	return 0;
}

int32_t
nufa_xattrop (call_frame_t *frame,
	     xlator_t *this,
	     loc_t *loc,
	     gf_xattrop_flags_t flags,
	     dict_t *dict)
{
	xlator_t     *subvol = NULL;
        int           op_errno = -1;
	nufa_local_t  *local = NULL;

        VALIDATE_OR_GOTO (frame, err);
        VALIDATE_OR_GOTO (this, err);
        VALIDATE_OR_GOTO (loc, err);
        VALIDATE_OR_GOTO (loc->inode, err);
        VALIDATE_OR_GOTO (loc->path, err);

	subvol = nufa_subvol_get_cached (this, loc->inode);
	if (!subvol) {
		gf_log (this->name, GF_LOG_ERROR,
			"no cached subvolume for path=%s", loc->path);
		op_errno = EINVAL;
		goto err;
	}

	local = nufa_local_init (frame);
	if (!local) {
		op_errno = ENOMEM;
		gf_log (this->name, GF_LOG_ERROR,
			"memory allocation failed :(");
		goto err;
	}

	local->inode = inode_ref (loc->inode);
	local->call_cnt = 1;

	STACK_WIND (frame,
		    nufa_xattrop_cbk,
		    subvol, subvol->fops->xattrop,
		    loc, flags, dict);

	return 0;

err:
	op_errno = (op_errno == -1) ? errno : op_errno;
	NUFA_STACK_UNWIND (frame, -1, op_errno, NULL);

	return 0;
}

static int32_t
nufa_fxattrop_cbk (call_frame_t *frame,
		  void *cookie,
		  xlator_t *this,
		  int32_t op_ret,
		  int32_t op_errno,
		  dict_t *dict)
{
	NUFA_STACK_UNWIND (frame, op_ret, op_errno, dict);
	return 0;
}

int32_t
nufa_fxattrop (call_frame_t *frame,
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

	subvol = nufa_subvol_get_cached (this, fd->inode);
	if (!subvol) {
		gf_log (this->name, GF_LOG_ERROR,
			"no cached subvolume for fd=%p", fd);
		op_errno = EINVAL;
		goto err;
	}

	STACK_WIND (frame,
		    nufa_fxattrop_cbk,
		    subvol, subvol->fops->fxattrop,
		    fd, flags, dict);

	return 0;

err:
	op_errno = (op_errno == -1) ? errno : op_errno;
	NUFA_STACK_UNWIND (frame, -1, op_errno, NULL);

	return 0;
}


static int32_t
nufa_inodelk_cbk (call_frame_t *frame, void *cookie,
		 xlator_t *this, int32_t op_ret, int32_t op_errno)

{
	NUFA_STACK_UNWIND (frame, op_ret, op_errno);
	return 0;
}


int32_t
nufa_inodelk (call_frame_t *frame, xlator_t *this,
	     loc_t *loc, int32_t cmd, struct flock *lock)
{
	xlator_t     *subvol = NULL;
        int           op_errno = -1;
	nufa_local_t  *local = NULL;


        VALIDATE_OR_GOTO (frame, err);
        VALIDATE_OR_GOTO (this, err);
        VALIDATE_OR_GOTO (loc, err);
        VALIDATE_OR_GOTO (loc->inode, err);
        VALIDATE_OR_GOTO (loc->path, err);

	subvol = nufa_subvol_get_cached (this, loc->inode);
	if (!subvol) {
		gf_log (this->name, GF_LOG_ERROR,
			"no cached subvolume for path=%s", loc->path);
		op_errno = EINVAL;
		goto err;
	}

	local = nufa_local_init (frame);
	if (!local) {
		op_errno = ENOMEM;
		gf_log (this->name, GF_LOG_ERROR,
			"memory allocation failed :(");
		goto err;
	}

	local->inode = inode_ref (loc->inode);
	local->call_cnt = 1;

	STACK_WIND (frame,
		    nufa_inodelk_cbk,
		    subvol, subvol->fops->inodelk,
		    loc, cmd, lock);

	return 0;

err:
	op_errno = (op_errno == -1) ? errno : op_errno;
	NUFA_STACK_UNWIND (frame, -1, op_errno);

	return 0;
}


static int32_t
nufa_finodelk_cbk (call_frame_t *frame, void *cookie,
		  xlator_t *this, int32_t op_ret, int32_t op_errno)

{
	NUFA_STACK_UNWIND (frame, op_ret, op_errno);
	return 0;
}


int32_t
nufa_finodelk (call_frame_t *frame, xlator_t *this,
	      fd_t *fd, int32_t cmd, struct flock *lock)
{
	xlator_t     *subvol = NULL;
        int           op_errno = -1;

        VALIDATE_OR_GOTO (frame, err);
        VALIDATE_OR_GOTO (this, err);
        VALIDATE_OR_GOTO (fd, err);

	subvol = nufa_subvol_get_cached (this, fd->inode);
	if (!subvol) {
		gf_log (this->name, GF_LOG_ERROR,
			"no cached subvolume for fd=%p", fd);
		op_errno = EINVAL;
		goto err;
	}


	STACK_WIND (frame,
		    nufa_finodelk_cbk,
		    subvol, subvol->fops->finodelk,
		    fd, cmd, lock);

	return 0;

err:
	op_errno = (op_errno == -1) ? errno : op_errno;
	NUFA_STACK_UNWIND (frame, -1, op_errno);

	return 0;
}


static int32_t
nufa_entrylk_cbk (call_frame_t *frame, void *cookie,
		 xlator_t *this, int32_t op_ret, int32_t op_errno)

{
	NUFA_STACK_UNWIND (frame, op_ret, op_errno);
	return 0;
}

int32_t
nufa_entrylk (call_frame_t *frame, xlator_t *this,
	     loc_t *loc, const char *basename,
	     entrylk_cmd cmd, entrylk_type type)
{
	xlator_t     *subvol = NULL;
        int           op_errno = -1;
	nufa_local_t  *local = NULL;

        VALIDATE_OR_GOTO (frame, err);
        VALIDATE_OR_GOTO (this, err);
        VALIDATE_OR_GOTO (loc, err);
        VALIDATE_OR_GOTO (loc->inode, err);
        VALIDATE_OR_GOTO (loc->path, err);

	subvol = nufa_subvol_get_cached (this, loc->inode);
	if (!subvol) {
		gf_log (this->name, GF_LOG_ERROR,
			"no cached subvolume for path=%s", loc->path);
		op_errno = EINVAL;
		goto err;
	}

	local = nufa_local_init (frame);
	if (!local) {
		op_errno = ENOMEM;
		gf_log (this->name, GF_LOG_ERROR,
			"memory allocation failed :(");
		goto err;
	}

	local->inode = inode_ref (loc->inode);
	local->call_cnt = 1;

	STACK_WIND (frame, nufa_entrylk_cbk,
		    subvol, subvol->fops->entrylk,
		    loc, basename, cmd, type);

	return 0;

err:
	op_errno = (op_errno == -1) ? errno : op_errno;
	NUFA_STACK_UNWIND (frame, -1, op_errno);

	return 0;
}

static int32_t
nufa_fentrylk_cbk (call_frame_t *frame, void *cookie,
		  xlator_t *this, int32_t op_ret, int32_t op_errno)

{
	NUFA_STACK_UNWIND (frame, op_ret, op_errno);
	return 0;
}

int32_t
nufa_fentrylk (call_frame_t *frame, xlator_t *this,
	      fd_t *fd, const char *basename,
	      entrylk_cmd cmd, entrylk_type type)
{
	xlator_t     *subvol = NULL;
        int           op_errno = -1;

        VALIDATE_OR_GOTO (frame, err);
        VALIDATE_OR_GOTO (this, err);
        VALIDATE_OR_GOTO (fd, err);

	subvol = nufa_subvol_get_cached (this, fd->inode);
	if (!subvol) {
		gf_log (this->name, GF_LOG_ERROR,
			"no cached subvolume for fd=%p", fd);
		op_errno = EINVAL;
		goto err;
	}

	STACK_WIND (frame, nufa_fentrylk_cbk,
		    subvol, subvol->fops->fentrylk,
		    fd, basename, cmd, type);

	return 0;

err:
	op_errno = (op_errno == -1) ? errno : op_errno;
	NUFA_STACK_UNWIND (frame, -1, op_errno);

	return 0;
}


int
nufa_forget (xlator_t *this, inode_t *inode)
{
	nufa_layout_t *layout = NULL;
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
nufa_init_subvolumes (xlator_t *this, nufa_conf_t *conf)
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
nufa_notify (xlator_t *this, int event, void *data, ...)
{
	xlator_t   *subvol = NULL;
	int         cnt    = -1;
	int         i      = -1;
	nufa_conf_t *conf   = NULL;
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

	ret = nufa_notify (this, event, data);

	return ret;
}


int
init (xlator_t *this)
{
	data_t        *data = NULL;
	xlator_list_t *trav = NULL;
        nufa_conf_t    *conf = NULL;
        int            ret = -1;
        int            i = 0;


        conf = calloc (1, sizeof (*conf));
        if (!conf) {
                gf_log (this->name, GF_LOG_ERROR,
                        "memory allocation failed :(");
                goto err;
        }

        ret = nufa_init_subvolumes (this, conf);
        if (ret == -1) {
                goto err;
        }

        ret = nufa_layouts_init (this, conf);
        if (ret == -1) {
                goto err;
        }

	LOCK_INIT (&conf->subvolume_lock);

	data = dict_get (this->options, "local-volume-name");
	if (data) {
		trav = this->children;
		while (trav) {
			if (strcmp (trav->xlator->name, data->data) == 0)
				break;
			trav = trav->next;
		}
		if (!trav) {
			gf_log (this->name, GF_LOG_ERROR, 
				"'local-volume-name' option not valid, can not continue");
			goto err;
		}

		/* The volume specified exists */
		conf->local_volume = trav->xlator;
	} else {
		gf_log (this->name, GF_LOG_ERROR, 
			"'local-volume-name' option not given, can't continue");
		goto err;
	}

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
	.lookup      = nufa_lookup,
	.stat        = nufa_stat,
	.chmod       = nufa_chmod,
	.chown       = nufa_chown,
	.fchown      = nufa_fchown,
	.fchmod      = nufa_fchmod,
	.fstat       = nufa_fstat,
	.utimens     = nufa_utimens,
	.truncate    = nufa_truncate,
	.ftruncate   = nufa_ftruncate,
	.access      = nufa_access,
	.readlink    = nufa_readlink,
	.setxattr    = nufa_setxattr,
	.getxattr    = nufa_getxattr,
	.removexattr = nufa_removexattr,
	.open        = nufa_open,
	.readv       = nufa_readv,
	.writev      = nufa_writev,
	.flush       = nufa_flush,
	.fsync       = nufa_fsync,
	.statfs      = nufa_statfs,
	.lk          = nufa_lk,
	.opendir     = nufa_opendir,
	.readdir     = nufa_readdir,
	.fsyncdir    = nufa_fsyncdir,
	.mknod       = nufa_mknod,
	.symlink     = nufa_symlink,
	.unlink      = nufa_unlink,
	.link        = nufa_link,
	.create      = nufa_create,
	.mkdir       = nufa_mkdir,
	.rmdir       = nufa_rmdir,
	.rename      = nufa_rename,
	.inodelk     = nufa_inodelk,
	.finodelk    = nufa_finodelk,
	.entrylk     = nufa_entrylk,
	.fentrylk    = nufa_fentrylk,
	.xattrop     = nufa_xattrop,
	.fxattrop    = nufa_fxattrop,
#if 0
	.setdents    = nufa_setdents,
	.getdents    = nufa_getdents,
	.checksum    = nufa_checksum,
#endif
};


struct xlator_mops mops = {
};


struct xlator_cbks cbks = {
//	.release    = nufa_release,
//      .releasedir = nufa_releasedir,
	.forget     = nufa_forget
};


struct xlator_options options[] = {
        { "algorithm", GF_OPTION_TYPE_STR, 0, },
	{ "local-volume-name", GF_OPTION_TYPE_XLATOR, 0, },
        { NULL, 0, 0, 0, 0 },
};
