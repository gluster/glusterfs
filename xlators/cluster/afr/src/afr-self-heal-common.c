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

#include "glusterfs.h"
#include "xlator.h"
#include "byte-order.h"

#include "afr.h"
#include "afr-transaction.h"
#include "afr-self-heal-common.h"
#include "afr-self-heal.h"


/**
 * select_source - select a source and return it
 * TODO: take into account option 'favorite-child'
 */

int
afr_sh_select_source (int sources[], int child_count)
{
	int i;
	for (i = 0; i < child_count; i++)
		if (sources[i])
			return i;

	return -1;
}


/**
 * sink_count - return number of sinks in sources array
 */

int
afr_sh_sink_count (int sources[], int child_count)
{
	int i;
	int sinks = 0;
	for (i = 0; i < child_count; i++)
		if (!sources[i])
			sinks++;
	return sinks;
}


void
afr_sh_print_pending_matrix (int32_t *pending_matrix[], xlator_t *this)
{
	afr_private_t * priv = this->private;

	char *buf = NULL;
	char *ptr = NULL;

	int i, j;

        /* 10 digits per entry + 1 space + '[' and ']' */
	buf = malloc (priv->child_count * 11 + 8); 

	for (i = 0; i < priv->child_count; i++) {
		ptr = buf;
		ptr += sprintf (ptr, "[ ");
		for (j = 0; j < priv->child_count; j++) {
			ptr += sprintf (ptr, "%d ", pending_matrix[i][j]);
		}
		ptr += sprintf (ptr, "]");
		gf_log (this->name, GF_LOG_DEBUG,
			"pending_matrix: %s", buf);
	}

	FREE (buf);
}


void
afr_sh_build_pending_matrix (int32_t *pending_matrix[], dict_t *xattr[],
			     int child_count)
{
	data_t *data = NULL;
	int i = 0;
	int j = 0;

	for (i = 0; i < child_count; i++) {
		if (xattr[i]) {
			data = dict_get (xattr[i], AFR_DATA_PENDING);
			if (data) {
				for (j = 0; j < child_count; j++) {
					pending_matrix[i][j] = 
						ntoh32 (((int32_t *)(data->data))[j]);
				}
			}
		}
	}
}


/**
 * mark_sources: Mark all 'source' nodes and return number of source
 * nodes found
 */

int
afr_sh_mark_sources (int32_t *pending_matrix[], int sources[], int child_count)
{
	int i = 0;
	int j = 0;

	int nsources = 0;

	/*
	  Let's 'normalize' the pending matrix first,
	  by disregarding all pending entries that refer
	  to themselves
	*/
	for (i = 0; i < child_count; i++) {
		pending_matrix[i][i] = 0;
	}

	for (i = 0; i < child_count; i++) {
		for (j = 0; j < child_count; j++) {
			if (pending_matrix[j][i])
				break;
		}

		if (j == child_count) {
			nsources++;
			sources[i] = 1;
		}
	}

	return nsources;
}

/**
 * is_matrix_zero - return true if pending matrix is all zeroes
 */

int
afr_sh_is_matrix_zero (int32_t *pending_matrix[], int child_count)
{
	int i, j;

	for (i = 0; i < child_count; i++) 
		for (j = 0; j < child_count; j++) 
			if (pending_matrix[i][j]) 
				return 0;
	return 1;
}

int
sh_missing_entries_unlck_cbk (call_frame_t *frame, void *cookie,
			      xlator_t *this,
			      int32_t op_ret, int32_t op_errno)
{
	afr_local_t     *local = NULL;
	afr_self_heal_t *sh = NULL;
	int              call_count = 0;


	local = frame->local;
	sh = &local->self_heal;

	LOCK (&frame->lock);
	{
		call_count = --local->call_count;
	}
	UNLOCK (&frame->lock);

	if (call_count == 0) {
		if (local->govinda_gOvinda) {
			gf_log (this->name, GF_LOG_DEBUG,
				"aborting selfheal of %s",
				local->loc.path);

			sh->completion_cbk (frame, this);
		} else {
			gf_log (this->name, GF_LOG_DEBUG,
				"proceeding to metadata check on %s",
				local->loc.path);
			afr_self_heal_metadata (frame, this);
		}
	}

	return 0;
}
			      

static int
sh_missing_entries_finish (call_frame_t *frame, xlator_t *this)
{
	afr_private_t      *priv = NULL;
	afr_local_t        *local = NULL;
	int                 i = 0;
	int                 call_count = 0;
	afr_self_heal_t    *sh = NULL;


	local = frame->local;
	sh = &local->self_heal;
	priv = this->private;

	call_count = local->child_count;

	local->call_count = call_count;

	for (i = 0; i < priv->child_count; i++) {
		if (local->child_up[i]) {
			gf_log (this->name, GF_LOG_DEBUG,
				"unlocking %lld/%s on subvolume %s",
				sh->parent_loc.inode->ino, local->loc.name,
				priv->children[i]->name);

			STACK_WIND (frame, sh_missing_entries_unlck_cbk,
				    priv->children[i],
				    priv->children[i]->fops->gf_dir_lk,
				    &sh->parent_loc, local->loc.name,
				    GF_DIR_LK_UNLOCK, GF_DIR_LK_WRLCK);

			if (!--call_count)
				break;
		}
	}
	return 0;
}


static int
sh_destroy_cbk (call_frame_t *frame, void *cookie, xlator_t *this,
		int32_t op_ret, int op_errno, struct stat *stbuf)
{
	STACK_DESTROY (frame->root);
	return 0;
}


static int
sh_missing_entries_newentry_cbk (call_frame_t *frame, void *cookie,
				 xlator_t *this,
				 int32_t op_ret, int32_t op_errno,
				 inode_t *inode, struct stat *stbuf)
{
	afr_local_t     *local = NULL;
	afr_self_heal_t *sh = NULL;
	afr_private_t   *priv = NULL;
	call_frame_t    *chown_frame = NULL;
	int              call_count = 0;
	int              child_index = 0;
	struct stat     *buf = NULL;


	local = frame->local;
	sh = &local->self_heal;
	priv = this->private;

	buf = &sh->buf[sh->source];
	child_index = (long) cookie;

	if (op_ret == 0) {
		chown_frame = copy_frame (frame);

		gf_log (this->name, GF_LOG_DEBUG,
			"chown %s to %d %d on subvolume %s",
			local->loc.path, buf->st_uid, buf->st_gid,
			priv->children[child_index]->name);

		STACK_WIND (chown_frame, sh_destroy_cbk,
			    priv->children[child_index],
			    priv->children[child_index]->fops->chown,
			    &local->loc,
			    buf->st_uid, buf->st_gid);
	}

	LOCK (&frame->lock);
	{
		call_count = --local->call_count;
	}
	UNLOCK (&frame->lock);

	if (call_count == 0) {
		sh_missing_entries_finish (frame, this);
	}

	return 0;
}


static int
sh_missing_entries_mknod (call_frame_t *frame, xlator_t *this)
{
	afr_local_t     *local = NULL;
	afr_self_heal_t *sh = NULL;
	afr_private_t   *priv = NULL;
	int              i = 0;
	int              enoent_count = 0;
	int              call_count = 0;
	mode_t           st_mode = 0;
	dev_t            st_dev = 0;


	local = frame->local;
	sh = &local->self_heal;
	priv = this->private;

	for (i = 0; i < priv->child_count; i++)
		if (sh->child_errno[i] == ENOENT)
			enoent_count++;

	call_count = enoent_count;
	local->call_count = call_count;

	st_mode = sh->buf[sh->source].st_mode;
	st_dev  = sh->buf[sh->source].st_dev;

	gf_log (this->name, GF_LOG_DEBUG,
		"mknod %s mode 0%o on %d subvolumes",
		local->loc.path, st_mode, enoent_count);

	for (i = 0; i < priv->child_count; i++) {
		if (sh->child_errno[i] == ENOENT) {
			STACK_WIND_COOKIE (frame,
					   sh_missing_entries_newentry_cbk,
					   (void *) (long) i,
					   priv->children[i],
					   priv->children[i]->fops->mknod,
					   &local->loc, st_mode, st_dev);
			if (!--call_count)
				break;
		}
	}

	return 0;
}


static int
sh_missing_entries_mkdir (call_frame_t *frame, xlator_t *this)
{
	afr_local_t     *local = NULL;
	afr_self_heal_t *sh = NULL;
	afr_private_t   *priv = NULL;
	int              i = 0;
	int              enoent_count = 0;
	int              call_count = 0;
	mode_t           st_mode = 0;


	local = frame->local;
	sh = &local->self_heal;
	priv = this->private;

	for (i = 0; i < priv->child_count; i++)
		if (sh->child_errno[i] == ENOENT)
			enoent_count++;

	call_count = enoent_count;
	local->call_count = call_count;

	st_mode = sh->buf[sh->source].st_mode;

	gf_log (this->name, GF_LOG_DEBUG,
		"mkdir %s mode 0%o on %d subvolumes",
		local->loc.path, st_mode, enoent_count);

	for (i = 0; i < priv->child_count; i++) {
		if (sh->child_errno[i] == ENOENT) {
			STACK_WIND_COOKIE (frame,
					   sh_missing_entries_newentry_cbk,
					   (void *) (long) i,
					   priv->children[i],
					   priv->children[i]->fops->mkdir,
					   &local->loc, st_mode);
			if (!--call_count)
				break;
		}
	}

	return 0;
}


static int
sh_missing_entries_symlink (call_frame_t *frame, xlator_t *this,
			    const char *link)
{
	afr_local_t     *local = NULL;
	afr_self_heal_t *sh = NULL;
	afr_private_t   *priv = NULL;
	int              i = 0;
	int              enoent_count = 0;
	int              call_count = 0;


	local = frame->local;
	sh = &local->self_heal;
	priv = this->private;

	for (i = 0; i < priv->child_count; i++)
		if (sh->child_errno[i] == ENOENT)
			enoent_count++;

	call_count = enoent_count;
	local->call_count = call_count;

	gf_log (this->name, GF_LOG_DEBUG,
		"symlink %s -> %s on %d subvolumes",
		local->loc.path, link, enoent_count);

	for (i = 0; i < priv->child_count; i++) {
		if (sh->child_errno[i] == ENOENT) {
			STACK_WIND_COOKIE (frame,
					   sh_missing_entries_newentry_cbk,
					   (void *) (long) i,
					   priv->children[i],
					   priv->children[i]->fops->symlink,
					   link, &local->loc);
			if (!--call_count)
				break;
		}
	}

	return 0;
}


static int
sh_missing_entries_readlink_cbk (call_frame_t *frame, void *cookie,
				 xlator_t *this,
				 int32_t op_ret, int32_t op_errno,
				 const char *link)
{
	if (op_ret == 0)
		sh_missing_entries_symlink (frame, this, link);
	else
		sh_missing_entries_finish (frame, this);

	return 0;
}


static int
sh_missing_entries_readlink (call_frame_t *frame, xlator_t *this)
{
	afr_local_t     *local = NULL;
	afr_self_heal_t *sh = NULL;
	afr_private_t   *priv = NULL;


	local = frame->local;
	sh = &local->self_heal;
	priv = this->private;

	STACK_WIND (frame, sh_missing_entries_readlink_cbk,
		    priv->children[sh->source],
		    priv->children[sh->source]->fops->readlink,
		    &local->loc, 4096);

	return 0;
}


static int
sh_missing_entries_create (call_frame_t *frame, xlator_t *this)
{
	afr_local_t     *local = NULL;
	afr_self_heal_t *sh = NULL;
	int              type = 0;
	int              i = 0;
	afr_private_t   *priv = NULL;
	int              enoent_count = 0;
	int              govinda_gOvinda = 0;


	local = frame->local;
	sh = &local->self_heal;
	priv = this->private;

	for (i = 0; i < priv->child_count; i++) {
		if (sh->child_errno[i]) {
			if (sh->child_errno[i] == ENOENT)
				enoent_count++;
		} else {
			if (type) {
				if (type != (sh->buf[i].st_mode & S_IFMT))
					govinda_gOvinda = 1;
			} else {
				sh->source = i;
				type = sh->buf[i].st_mode & S_IFMT;
			}
		}
	}

	if (govinda_gOvinda) {
		gf_log (this->name, GF_LOG_ERROR,
			"conflicing filetypes exist for path %s. returning.",
			local->loc.path);

		local->govinda_gOvinda = 1;
		sh_missing_entries_finish (frame, this);
		return 0;
	}

	if (!type) {
		gf_log (this->name, GF_LOG_ERROR,
			"no source found for %s. all nodes down?. returning.",
			local->loc.path);
		/* subvolumes down and/or file does not exist */
		sh_missing_entries_finish (frame, this);
		return 0;
	}

	if (enoent_count == 0) {
		gf_log (this->name, GF_LOG_ERROR,
			"no missing files - %s. proceeding to metadata check",
			local->loc.path);
		/* proceed to next step - metadata self-heal */
		sh_missing_entries_finish (frame, this);
		return 0;
	}

	switch (type) {
	case S_IFSOCK:
	case S_IFREG:
	case S_IFBLK:
	case S_IFCHR:
	case S_IFIFO:
		sh_missing_entries_mknod (frame, this);
		break;
	case S_IFLNK:
		sh_missing_entries_readlink (frame, this);
	case S_IFDIR:
		sh_missing_entries_mkdir (frame, this);
		break;
	default:
		gf_log (this->name, GF_LOG_ERROR,
			"unknown file type: 0%o", type);
		local->govinda_gOvinda = 1;
		sh_missing_entries_finish (frame, this);
	}

	return 0;
}


static int
sh_missing_entries_lookup_cbk (call_frame_t *frame, void *cookie,
			       xlator_t *this,
			       int32_t op_ret, int32_t op_errno,
			       inode_t *inode, struct stat *buf, dict_t *xattr)
{
	int              child_index = 0;
	afr_local_t     *local = NULL;
	int              call_count = 0;
	afr_self_heal_t *sh = NULL;
	afr_private_t   *priv = NULL;


	local = frame->local;
	sh = &local->self_heal;
	priv = this->private;

	child_index = (long) cookie;

	LOCK (&frame->lock);
	{
		call_count = --local->call_count;

		if (op_ret == 0) {
			gf_log (this->name, GF_LOG_DEBUG,
				"path %s on subvolume %s is of mode 0%o",
				local->loc.path,
				priv->children[child_index]->name,
				buf->st_mode);

			local->self_heal.buf[child_index] = *buf;
		} else {
			gf_log (this->name, GF_LOG_DEBUG,
				"path %s on subvolume %s => -1 (%s)",
				local->loc.path,
				priv->children[child_index]->name,
				strerror (op_errno));

			local->self_heal.child_errno[child_index] = op_errno;
		}

	}
	UNLOCK (&frame->lock);

	if (call_count == 0) {
		sh_missing_entries_create (frame, this);
	}

	return 0;
}


static int
sh_missing_entries_lookup (call_frame_t *frame, xlator_t *this)
{
	afr_local_t    *local = NULL;
	int             i = 0;
	int             call_count = 0;
	afr_private_t  *priv = NULL;


	local = frame->local;
	call_count = local->child_count;
	priv = this->private;

	local->call_count = call_count;
	local->self_heal.buf = calloc (priv->child_count,
				       sizeof (struct stat));
	local->self_heal.child_errno = calloc (priv->child_count,
					       sizeof (int));


	for (i = 0; i < priv->child_count; i++) {
		if (local->child_up[i]) {
			gf_log (this->name, GF_LOG_DEBUG,
				"looking up %s on subvolume %s",
				local->loc.path, priv->children[i]->name);

			STACK_WIND_COOKIE (frame,
					   sh_missing_entries_lookup_cbk,
					   (void *) (long) i,
					   priv->children[i],
					   priv->children[i]->fops->lookup,
					   &local->loc, 0);

			if (!--call_count)
				break;
		}
	}

	return 0;
}


static int
sh_missing_entries_lk_cbk (call_frame_t *frame, void *cookie, xlator_t *this,
			   int32_t op_ret, int32_t op_errno)
{
	int          call_count = 0;
	afr_local_t *local = NULL;


	local = frame->local;

	/* TODO: what if lock fails */
	LOCK (&frame->lock);
	{
		call_count = --local->call_count;
	}
	UNLOCK (&frame->lock);

	if (call_count == 0) {
		sh_missing_entries_lookup (frame, this);
	}

	return 0;
}


static int
afr_self_heal_missing_entries (call_frame_t *frame, xlator_t *this)
{
	afr_local_t     *local = NULL;
	afr_self_heal_t *sh = NULL;
	afr_private_t   *priv = NULL;
	int              i = 0;
	int              call_count = 0;


	local = frame->local;
	sh = &local->self_heal;
	priv = this->private;

	gf_log (this->name, GF_LOG_DEBUG,
		"attempting to recreate missing entries for path=%s",
		local->loc.path);

	build_parent_loc (&sh->parent_loc, &local->loc);

	call_count = local->child_count;

	local->call_count = call_count;

	for (i = 0; i < priv->child_count; i++) {
		if (local->child_up[i]) {
			STACK_WIND (frame, sh_missing_entries_lk_cbk,
				    priv->children[i],
				    priv->children[i]->fops->gf_dir_lk,
				    &sh->parent_loc, local->loc.name,
				    GF_DIR_LK_LOCK, GF_DIR_LK_WRLCK);
			if (!--call_count)
				break;
		}
 	}

	return 0;
}


int
afr_self_heal (call_frame_t *frame, xlator_t *this,
	       int (*completion_cbk) (call_frame_t *, xlator_t *))
{
	afr_local_t     *local = NULL;
	afr_self_heal_t *sh = NULL;


	local = frame->local;
	sh = &local->self_heal;

	sh->completion_cbk = completion_cbk;

	if (local->success_count && local->enoent_count) {
		afr_self_heal_missing_entries (frame, this);
	} else {
		gf_log (this->name, GF_LOG_DEBUG,
			"proceeding to metadata check on %s",
			local->loc.path);
		afr_self_heal_metadata (frame, this);
	}

	return 0;
}
