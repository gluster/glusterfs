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

int
afr_sh_source_count (int sources[], int child_count)
{
	int i;
	int nsource = 0;

	for (i = 0; i < child_count; i++)
		if (sources[i])
			nsource++;
	return nsource;
}


int
afr_sh_supress_errenous_children (int sources[], int child_errno[],
				  int child_count)
{
	int i = 0;

	for (i = 0; i < child_count; i++) {
		if (child_errno[i] && sources[i]) {
			sources[i] = 0;
		}
	}

	return 0;
}


int
afr_sh_supress_empty_children (int sources[], dict_t *xattr[],
			       struct stat *buf,
			       int child_count, const char *key)
{
	int      i = 0;
	int32_t *pending = NULL;
	int      ret = 0;
	int      all_xattr_missing = 1;

	/* if the file was created by afr with xattrs */
	for (i = 0; i < child_count; i++) {
		if (!xattr[i])
			continue;

		ret = dict_get_ptr (xattr[i], (char *)key, VOID(&pending));
		if (ret != 0) {
			continue;
		}

		all_xattr_missing = 0;
		break;
	}

	if (all_xattr_missing) {
		/* supress 0byte files.. this avoids empty file created
		   by dir selfheal to overwrite the 'good' file */
		for (i = 0; i < child_count; i++) {
			if (!buf[i].st_size)
				sources[i] = 0;
		}
		goto out;
	}


	for (i = 0; i < child_count; i++) {
		if (!xattr[i]) {
			sources[i] = 0;
			continue;
		}

		ret = dict_get_ptr (xattr[i], (char *)key, VOID(&pending));
		if (ret != 0) {
			sources[i] = 0;
			continue;
		}

		if (!pending) {
			sources[i] = 0;
			continue;
		}
	}

out:
	return 0;
}


void
afr_sh_print_pending_matrix (int32_t *pending_matrix[], xlator_t *this)
{
	afr_private_t * priv = this->private;

	char *buf = NULL;
	char *ptr = NULL;

	int i, j;

        /* 10 digits per entry + 1 space + '[' and ']' */
	buf = MALLOC (priv->child_count * 11 + 8); 

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
			     int child_count, const char *key)
{
	int i = 0;
	int j = 0;
	int32_t *pending = NULL;
	int ret = -1;

	/* start clean */
	for (i = 0; i < child_count; i++) {
		for (j = 0; j < child_count; j++) {
			pending_matrix[i][j] = 0;
		}
	}

	for (i = 0; i < child_count; i++) {
		if (!xattr[i])
			continue;

		pending = NULL;

		ret = dict_get_ptr (xattr[i], (char *) key,
				    VOID(&pending));
		if (ret != 0)
			continue;

		for (j = 0; j < child_count; j++) {
			pending_matrix[i][j] = ntoh32 (pending[j]);
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


	/* start clean */
	for (i = 0; i < child_count; i++) {
		sources[i] = 0;
	}

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


void
afr_sh_pending_to_delta (int32_t *pending_matrix[], int32_t *delta_matrix[],
			 int success[], int child_count)
{
	int i = 0;
	int j = 0;

	/* start clean */
	for (i = 0; i < child_count; i++) {
		for (j = 0; j < child_count; j++) {
			delta_matrix[i][j] = 0;
		}
	}

	for (i = 0; i < child_count; i++) {
		for (j = 0; j < child_count; j++) {
			if (!success[j])
				continue;
			delta_matrix[i][j] = -pending_matrix[i][j];
		}
	}
}


int
afr_sh_delta_to_xattr (int32_t *delta_matrix[], dict_t *xattr[],
		       int child_count, const char *key)
{
	int i = 0;
	int j = 0;

	int ret = 0;

	int32_t *pending = 0;

	for (i = 0; i < child_count; i++) {
		if (!xattr[i])
			continue;

		pending = CALLOC (sizeof (int32_t), child_count);
		for (j = 0; j < child_count; j++) {
			pending[j] = hton32 (delta_matrix[i][j]);
		}

		ret = dict_set_bin (xattr[i], (char *) key, pending,
				    child_count * sizeof (int32_t));
	}

	return 0;
}


int
afr_sh_has_metadata_pending (dict_t *xattr, int child_count, xlator_t *this)
{
	afr_private_t *priv = NULL;
	int32_t       *pending = NULL;
	void          *tmp_pending = NULL; /* This is required to remove 'type-punned' warnings from gcc */

	int           ret = -1;
	int            i  = 0;

	priv = this->private;

	ret = dict_get_ptr (xattr, AFR_METADATA_PENDING, &tmp_pending);

	if (ret != 0)
		return 0;

	pending = tmp_pending;
	for (i = 0; i < priv->child_count; i++) {
		if (i == child_count)
			continue;
		if (pending[i])
			return 1;
	}

	return 0;
}


int
afr_sh_has_data_pending (dict_t *xattr, int child_count, xlator_t *this)
{
	afr_private_t *priv = NULL;
	int32_t       *pending = NULL;
	void          *tmp_pending = NULL; /* This is required to remove 'type-punned' warnings from gcc */

	int          ret = -1;
	int            i = 0;

	priv = this->private;

	ret = dict_get_ptr (xattr, AFR_DATA_PENDING, &tmp_pending);

	if (ret != 0)
		return 0;

	pending = tmp_pending;
	for (i = 0; i < priv->child_count; i++) {
		if (i == child_count)
			continue;
		if (pending[i])
			return 1;
	}

	return 0;
}


int
afr_sh_has_entry_pending (dict_t *xattr, int child_count, xlator_t *this)
{
	afr_private_t *priv = NULL;
	int32_t       *pending = NULL;
	void          *tmp_pending = NULL; /* This is required to remove 'type-punned' warnings from gcc */
	
	int          ret = -1;
	int            i = 0;

	priv = this->private;

	ret = dict_get_ptr (xattr, AFR_ENTRY_PENDING, &tmp_pending);

	if (ret != 0)
		return 0;

	pending = tmp_pending;
	for (i = 0; i < priv->child_count; i++) {
		if (i == child_count)
			continue;
		if (pending[i])
			return 1;
	}

	return 0;
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
afr_sh_missing_entries_done (call_frame_t *frame, xlator_t *this)
{
	afr_local_t     *local = NULL;
	afr_self_heal_t *sh = NULL;
	afr_private_t   *priv = NULL;
	int              i = 0;

	local = frame->local;
	sh = &local->self_heal;
	priv = this->private;

//	memset (sh->child_errno, 0, sizeof (int) * priv->child_count);
	memset (sh->buf, 0, sizeof (struct stat) * priv->child_count);
	
	for (i = 0; i < priv->child_count; i++) {
		if (sh->xattr[i])
			dict_unref (sh->xattr[i]);
		sh->xattr[i] = NULL;
	}

	if (local->govinda_gOvinda) {
		gf_log (this->name, GF_LOG_WARNING,
			"aborting selfheal of %s",
			local->loc.path);
		sh->completion_cbk (frame, this);
	} else {
		gf_log (this->name, GF_LOG_DEBUG,
			"proceeding to metadata check on %s",
			local->loc.path);
		afr_self_heal_metadata (frame, this);
	}

	return 0;
}


int
sh_missing_entries_unlck_cbk (call_frame_t *frame, void *cookie,
			      xlator_t *this,
			      int32_t op_ret, int32_t op_errno)
{
	afr_local_t     *local = NULL;
	afr_self_heal_t *sh = NULL;
	afr_private_t   *priv = NULL;
	int              call_count = 0;


	local = frame->local;
	sh = &local->self_heal;
	priv = this->private;

	LOCK (&frame->lock);
	{
	}
	UNLOCK (&frame->lock);

	call_count = afr_frame_return (frame);

	if (call_count == 0) {
		afr_sh_missing_entries_done (frame, this);
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
				"unlocking %"PRId64"/%s on subvolume %s",
				sh->parent_loc.inode->ino, local->loc.name,
				priv->children[i]->name);

			STACK_WIND (frame, sh_missing_entries_unlck_cbk,
				    priv->children[i],
				    priv->children[i]->fops->entrylk,
				    &sh->parent_loc, local->loc.name,
				    ENTRYLK_UNLOCK, ENTRYLK_WRLCK);

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
	}
	UNLOCK (&frame->lock);

	call_count = afr_frame_return (frame);

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
	if (op_ret > 0)
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
		break;
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
		if (op_ret == 0) {
			gf_log (this->name, GF_LOG_DEBUG,
				"path %s on subvolume %s is of mode 0%o",
				local->loc.path,
				priv->children[child_index]->name,
				buf->st_mode);

			local->self_heal.buf[child_index] = *buf;
		} else {
			gf_log (this->name, GF_LOG_WARNING,
				"path %s on subvolume %s => -1 (%s)",
				local->loc.path,
				priv->children[child_index]->name,
				strerror (op_errno));

			local->self_heal.child_errno[child_index] = op_errno;
		}

	}
	UNLOCK (&frame->lock);

	call_count = afr_frame_return (frame);

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
	dict_t         *xattr_req = NULL;
	int             ret = -1;

	local = frame->local;
	call_count = local->child_count;
	priv = this->private;

	local->call_count = call_count;
	
	xattr_req = dict_new();
	
	if (xattr_req)
		ret = dict_set_uint64 (xattr_req, AFR_ENTRY_PENDING,
				       priv->child_count * sizeof(int32_t));

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
					   &local->loc, xattr_req);

			if (!--call_count)
				break;
		}
	}
	
	if (xattr_req)
		dict_unref (xattr_req);

	return 0;
}


static int
sh_missing_entries_lk_cbk (call_frame_t *frame, void *cookie, xlator_t *this,
			   int32_t op_ret, int32_t op_errno)
{
	afr_local_t     *local = NULL;
	afr_self_heal_t *sh = NULL;
	int              call_count = 0;
	int              child_index = (long) cookie;


	local = frame->local;
	sh    = &local->self_heal;

	LOCK (&frame->lock);
	{
		if (op_ret == -1) {
			sh->op_failed = 1;

			gf_log (this->name,
				(op_errno == EAGAIN ? GF_LOG_DEBUG : GF_LOG_ERROR),
				"locking inode of %s on child %d failed: %s",
				local->loc.path, child_index,
				strerror (op_errno));
		} else {
			gf_log (this->name, GF_LOG_DEBUG,
				"inode of %s on child %d locked",
				local->loc.path, child_index);
		}
	}
	UNLOCK (&frame->lock);

	call_count = afr_frame_return (frame);

	if (call_count == 0) {
		if (sh->op_failed == 1) {
			sh_missing_entries_finish (frame, this);
			return 0;
		}

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

	afr_build_parent_loc (&sh->parent_loc, &local->loc);

	call_count = local->child_count;

	local->call_count = call_count;

	for (i = 0; i < priv->child_count; i++) {
		if (local->child_up[i]) {
			STACK_WIND (frame, sh_missing_entries_lk_cbk,
				    priv->children[i],
				    priv->children[i]->fops->entrylk,
				    &sh->parent_loc, local->loc.name,
				    ENTRYLK_LOCK_NB, ENTRYLK_WRLCK);
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
	afr_private_t   *priv = NULL;
	int              i = 0;


	local = frame->local;
	sh = &local->self_heal;
	priv = this->private;

	gf_log (this->name, GF_LOG_DEBUG,
		"performing self heal on %s (metadata=%d data=%d entry=%d)",
		local->loc.path,
		local->need_metadata_self_heal,
		local->need_data_self_heal,
		local->need_entry_self_heal);

	sh->completion_cbk = completion_cbk;

	sh->buf = CALLOC (priv->child_count, sizeof (struct stat));
	sh->child_errno = CALLOC (priv->child_count, sizeof (int));
	sh->success = CALLOC (priv->child_count, sizeof (int));
	sh->xattr = CALLOC (priv->child_count, sizeof (dict_t *));
	sh->sources = CALLOC (sizeof (*sh->sources), priv->child_count);

	sh->pending_matrix = CALLOC (sizeof (int32_t *), priv->child_count);
	for (i = 0; i < priv->child_count; i++) {
		sh->pending_matrix[i] = CALLOC (sizeof (int32_t),
						priv->child_count);
	}

	sh->delta_matrix = CALLOC (sizeof (int32_t *), priv->child_count);
	for (i = 0; i < priv->child_count; i++) {
		sh->delta_matrix[i] = CALLOC (sizeof (int32_t),
					      priv->child_count);
	}

	if (local->success_count && local->enoent_count) {
		afr_self_heal_missing_entries (frame, this);
	} else {
		gf_log (this->name, GF_LOG_DEBUG,
			"proceeding to metadata check on %s",
			local->loc.path);
		afr_sh_missing_entries_done (frame, this);
	}

	return 0;
}
