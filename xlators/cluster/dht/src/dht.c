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
   - use NS locks
   - complete dht_itransform
   - handle all cases in self heal layout reconstruction
   - complete linkfile selfheal
   - make readdir use scaled offset
   - return inode num consistantly in all cases
   - return directory size consistantly
   - get callback from directory selfheal (for rmdir/mkdir)
   - inode_ctx_set - static and dyn layouts
   - call_frame_t create/free API to be used everywhere
*/

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

		layout = local->layout;

		ret = dht_layout_merge (this, layout, prev->this,
					op_ret, op_errno, xattr);

		if (op_ret == -1)
			goto unlock;

        }
unlock:
        UNLOCK (&frame->lock);


        this_call_cnt = dht_frame_return (frame);

        if (is_last_call (this_call_cnt)) {
		if (local->op_ret == 0) {
			layout        = local->layout;

			ret = dht_layout_normalize (this, &local->loc, layout);

			if (ret != 0) {
				gf_log (this->name, GF_LOG_ERROR,
					"triggering selfheal on %s",
					local->loc.path);
				goto selfheal;
			}

			local->layout = NULL;

			inode_ctx_set (local->inode, this, layout);
		}

		DHT_STACK_UNWIND (frame, local->op_ret, local->op_errno,
				  local->inode, &local->stbuf, local->xattr);
        }

	return 0;

selfheal:
	ret = dht_selfheal_directory (frame, &local->loc, layout);

	return 0;
}


int
dht_lookup_linkfile_cbk (call_frame_t *frame, void *cookie,
                         xlator_t *this, int op_ret, int op_errno,
                         inode_t *inode, struct stat *stbuf, dict_t *xattr)
{
        call_frame_t *prev = NULL;


        prev = cookie;
        /* TODO: assert type is non-dir and non-linkfile */

        if (op_ret == -1)
                goto out;

        stbuf->st_ino = dht_itransform (this, prev->this, stbuf->st_ino);

out:
        DHT_STACK_UNWIND (frame, op_ret, op_errno, inode, stbuf, xattr);

        return 0;
}


int
dht_revalidate_cbk (call_frame_t *frame, void *cookie, xlator_t *this,
                    int op_ret, int op_errno,
                    inode_t *inode, struct stat *stbuf, dict_t *xattr)
{
        call_frame_t *prev = NULL;


        if (op_ret == -1)
                goto out;

        prev = cookie;

        stbuf->st_ino = dht_itransform (this, prev->this, stbuf->st_ino);

        /* add checks to see if directory was recreated with same
           inode number */

out:
        /* TODO: self-heal for missing files */
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

        if (op_ret == -1)
                goto out;

        is_linkfile = check_is_linkfile (inode, stbuf, xattr);
        is_dir      = check_is_dir (inode, stbuf, xattr);

        if (!is_dir && !is_linkfile) {
                /* non-directory and not a linkfile */

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

                stbuf->st_ino = dht_itransform (this, prev->this,
                                                     stbuf->st_ino);
                local->stbuf = *stbuf;
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
                        gf_log (this->name, GF_LOG_ERROR,
                                "linkfile not having link subvolume. path=%s",
                                loc->path);
                        op_ret   = -1;
                        op_errno = EINVAL;
                        goto out;
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
        dht_local_t  *local  = NULL;
        int           ret    = -1;
        int           op_errno = -1;


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

        if (is_revalidate (loc)) {
                subvol = dht_subvol_get_cached (this, loc->inode);

                if (!subvol) {
                        gf_log (this->name, GF_LOG_ERROR,
                                "revalidate without cache. path=%s",
                                loc->path);
                        op_errno = EINVAL;
                        goto err;
                }

                STACK_WIND (frame, dht_revalidate_cbk,
                            subvol, subvol->fops->lookup,
                            loc, need_xattr);
        } else {
                subvol = dht_subvol_get_hashed (this, loc);

                if (!subvol) {
                        gf_log (this->name, GF_LOG_ERROR,
                                "no subvolume in layout for path=%s",
                                loc->path);
                        op_errno = EINVAL;
                        goto err;
                }

                STACK_WIND (frame, dht_lookup_cbk,
                            subvol, subvol->fops->lookup,
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

		local->stbuf = *stbuf;
		local->op_ret = 0;
		local->stbuf.st_ino = dht_itransform (this, prev->this,
							   stbuf->st_ino);
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

	STACK_WIND (frame, dht_attr_cbk,
		    subvol, subvol->fops->stat,
		    loc);

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
			"local allocation failed :(");
		goto err;
	}

	local->call_cnt = 1;

	STACK_WIND (frame, dht_attr_cbk,
		    subvol, subvol->fops->fstat,
		    fd);

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

	local->fd = fd;
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

	local->fd = fd;
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


int
dht_gf_lk_cbk (call_frame_t *frame, void *cookie, xlator_t *this,
	    int op_ret, int op_errno, struct flock *flock)
{
        DHT_STACK_UNWIND (frame, op_ret, op_errno, flock);

        return 0;
}


int
dht_gf_lk (call_frame_t *frame, xlator_t *this,
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

	STACK_WIND (frame, dht_gf_lk_cbk,
		    subvol, subvol->fops->gf_lk,
		    fd, cmd, flock);

	return 0;

err:
	op_errno = (op_errno == -1) ? errno : op_errno;
	DHT_STACK_UNWIND (frame, -1, op_errno, NULL);

	return 0;
}


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

	local->fd = fd;
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
		 int op_ret, int op_errno, gf_dirent_t *entries)
{
	DHT_STACK_UNWIND (frame, op_ret, op_errno, entries);

        return 0;
}



int
dht_readdir (call_frame_t *frame, xlator_t *this,
	     fd_t *fd, size_t size, off_t offset)
{
	dht_local_t  *local  = NULL;
	dht_conf_t   *conf = NULL;
        int           op_errno = -1;


        VALIDATE_OR_GOTO (frame, err);
        VALIDATE_OR_GOTO (this, err);
        VALIDATE_OR_GOTO (fd, err);

	conf = this->private;

	local = dht_local_init (frame);

	local->fd = fd;

	/* TODO: do proper readdir */
	STACK_WIND (frame, dht_readdir_cbk,
		    conf->subvolumes[0],
		    conf->subvolumes[0]->fops->readdir,
		    fd, size, offset);

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

	local->fd = fd;
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
	stbuf->st_ino = dht_itransform (this, prev->this, stbuf->st_ino);
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
dht_rmelem (call_frame_t *frame, xlator_t *this, const char *path)
{
	dht_local_t  *local  = NULL;
	dht_conf_t   *conf = NULL;
        int           op_errno = -1;
	int           i = -1;


        VALIDATE_OR_GOTO (frame, err);
        VALIDATE_OR_GOTO (this, err);
        VALIDATE_OR_GOTO (path, err);

	conf = this->private;

	local = dht_local_init (frame);
	local->call_cnt = conf->subvolume_cnt;

	for (i = 0; i < conf->subvolume_cnt; i++) {
		STACK_WIND (frame, dht_err_cbk,
			    conf->subvolumes[i],
			    conf->subvolumes[i]->fops->rmelem, path);
	}

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
	stbuf->st_ino = dht_itransform (this, prev->this, stbuf->st_ino);
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
		if (op_ret == -1) {
			local->op_errno = op_errno;
			goto unlock;
		}
		local->op_ret = 0;

		local->stbuf = *stbuf;

		ret = dht_layout_merge (this, layout, prev->this,
					op_ret, op_errno, NULL);
	}
unlock:
	UNLOCK (&frame->lock);


	this_call_cnt = dht_frame_return (frame);
	if (is_last_call (this_call_cnt))
		dht_selfheal_directory (frame, &local->loc, local->layout);

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
dht_rmdir_cbk (call_frame_t *frame, void *cookie, xlator_t *this,
	       int op_ret, int op_errno)
{
	dht_local_t  *local = NULL;
	int           this_call_cnt = 0;
	call_frame_t *prev = NULL;
	dht_layout_t *layout = NULL;


	local = frame->local;
	prev  = cookie;
	layout = local->layout;

	LOCK (&frame->lock);
	{
		if (op_ret == -1) {
			local->op_errno = op_errno;
			local->op_ret   = -1;

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
	if (is_last_call (this_call_cnt))
		DHT_STACK_UNWIND (frame, local->op_ret, local->op_errno);

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

	for (i = 0; i < conf->subvolume_cnt; i++) {
		STACK_WIND (frame, dht_rmdir_cbk,
			    conf->subvolumes[i],
			    conf->subvolumes[i]->fops->rmdir, loc);
	}

	return 0;

err:
	op_errno = (op_errno == -1) ? errno : op_errno;
	DHT_STACK_UNWIND (frame, -1, op_errno);

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
        int            ret = -1;
        int            i = 0;


        conf = calloc (1, sizeof (*conf));
        if (!conf) {
                gf_log (this->name, GF_LOG_ERROR,
                        "memory allocation failed :(");
                goto err;
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
	.gf_lk       = dht_gf_lk,
	.opendir     = dht_opendir,
	.readdir     = dht_readdir,
	.fsyncdir    = dht_fsyncdir,
	.mknod       = dht_mknod,
	.symlink     = dht_symlink,
	.unlink      = dht_unlink,
	.rmelem      = dht_rmelem,
	.link        = dht_link,
	.create      = dht_create,
	.mkdir       = dht_mkdir,
	.rmdir       = dht_rmdir,
#if 0


  .rename      = dht_rename,
  .getdents    = dht_getdents,
  .checksum    = dht_checksum,
#endif
};


struct xlator_mops mops = {
};


struct xlator_cbks cbks = {
//	.release    = dht_release,
//      .releasedir = dht_releasedir,
//	.forget     = dht_forget
};


struct xlator_options options[] = {
        { "algorithm", GF_OPTION_TYPE_STR, 1, 0, 0 },
        { NULL, 0, 0, 0, 0 },
};
