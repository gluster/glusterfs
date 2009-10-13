/*
  Copyright (c) 2008-2009 Gluster, Inc. <http://www.gluster.com>
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
		gf_log (this->name, GF_LOG_TRACE,
			"pending_matrix: %s", buf);
	}

	FREE (buf);
}


void
afr_sh_build_pending_matrix (afr_private_t *priv,
                             int32_t *pending_matrix[], dict_t *xattr[],
			     int child_count, afr_transaction_type type)
{
	int i, j, k;

	int32_t *pending = NULL;
	int ret = -1;

        unsigned char *ignorant_subvols = NULL;

        ignorant_subvols = CALLOC (sizeof (*ignorant_subvols), child_count);

	/* start clean */
	for (i = 0; i < child_count; i++) {
		for (j = 0; j < child_count; j++) {
			pending_matrix[i][j] = 0;
		}
	}

	for (i = 0; i < child_count; i++) {
		pending = NULL;

                for (j = 0; j < child_count; j++) {
                        ret = dict_get_ptr (xattr[i], priv->pending_key[j],
                                            VOID(&pending));
                        
                        if (ret != 0) {
                                /*
                                 * There is no xattr present. This means this
                                 * subvolume should be considered an 'ignorant'
                                 * subvolume.
                                 */

                                ignorant_subvols[i] = 1;
                                continue;
                        }

                        k = afr_index_for_transaction_type (type);
                        
                        pending_matrix[i][j] = ntoh32 (pending[k]);
                }
	}

        /*
         * Make all non-ignorant subvols point towards the ignorant
         * subvolumes.
         */

        for (i = 0; i < child_count; i++) {
                if (ignorant_subvols[i]) {
                        for (j = 0; j < child_count; j++) {
                                if (!ignorant_subvols[j])
                                        pending_matrix[j][i] += 1;
                        }
                }
        }

        FREE (ignorant_subvols);
}


/**
 * mark_sources: Mark all 'source' nodes and return number of source
 * nodes found
 *
 * A node (a row in the pending matrix) belongs to one of
 * three categories:
 *
 * M is the pending matrix.
 *
 * 'innocent' - M[i] is all zeroes
 * 'fool'     - M[i] has i'th element = 1 (self-reference)
 * 'wise'     - M[i] has i'th element = 0, others are 1 or 0.
 *
 * All 'innocent' nodes are sinks. If all nodes are innocent, no self-heal is
 * needed.
 *
 * A 'wise' node can be a source. If two 'wise' nodes conflict, it is 
 * a split-brain. If one wise node refers to the other but the other doesn't
 * refer back, the referrer is a source.
 *
 * All fools are sinks, unless there are no 'wise' nodes. In that case,
 * one of the fools is made a source.
 */

typedef enum {
        AFR_NODE_INNOCENT,
        AFR_NODE_FOOL,
        AFR_NODE_WISE
} afr_node_type;

typedef struct {
        afr_node_type type;
        int           wisdom;
} afr_node_character;


static int
afr_sh_is_innocent (int32_t *array, int child_count)
{
        int i   = 0;
        int ret = 1;   /* innocent until proven guilty */

        for (i = 0; i < child_count; i++) {
                if (array[i]) {
                        ret = 0;
                        break;
                }
        }

        return ret;
}


static int
afr_sh_is_fool (int32_t *array, int i, int child_count)
{
        return array[i];   /* fool if accuses itself */
}


static int
afr_sh_is_wise (int32_t *array, int i, int child_count)
{
        return !array[i];  /* wise if does not accuse itself */
}


static int
afr_sh_all_nodes_innocent (afr_node_character *characters, 
                           int child_count)
{
        int i   = 0;
        int ret = 1;

        for (i = 0; i < child_count; i++) {
                if (characters[i].type != AFR_NODE_INNOCENT) {
                        ret = 0;
                        break;
                }
        }

        return ret;
}


static int
afr_sh_wise_nodes_exist (afr_node_character *characters, int child_count)
{
        int i   = 0;
        int ret = 0;

        for (i = 0; i < child_count; i++) {
                if (characters[i].type == AFR_NODE_WISE) {
                        ret = 1;
                        break;
                }
        }

        return ret;
}


/*
 * The 'wisdom' of a wise node is 0 if any other wise node accuses it.
 * It is 1 if no other wise node accuses it. 
 * Only wise nodes with wisdom 1 are sources.
 *
 * If no nodes with wisdom 1 exist, a split-brain has occured.
 */

static void
afr_sh_compute_wisdom (int32_t *pending_matrix[],
                       afr_node_character characters[], int child_count)
{
        int i = 0;
        int j = 0;

        for (i = 0; i < child_count; i++) {
                if (characters[i].type == AFR_NODE_WISE) {
                        characters[i].wisdom = 1;

                        for (j = 0; j < child_count; j++) {
                                if ((characters[j].type == AFR_NODE_WISE)
                                    && pending_matrix[j][i]) {
                                        
                                        characters[i].wisdom = 0;
                                }
                        }
                }
        }
}


static int
afr_sh_wise_nodes_conflict (afr_node_character *characters, 
                            int child_count)
{
        int i   = 0;
        int ret = 1;

        for (i = 0; i < child_count; i++) {
                if ((characters[i].type == AFR_NODE_WISE)
                    && characters[i].wisdom == 1) {

                        /* There is atleast one bona-fide wise node */
                        ret = 0;
                        break;
                }
        }

        return ret;
}


static int
afr_sh_mark_wisest_as_sources (int sources[], 
                               afr_node_character *characters, 
                               int child_count)
{
        int nsources = 0;
        
        int i = 0;

        for (i = 0; i < child_count; i++) {
                if (characters[i].wisdom == 1) {
                        sources[i] = 1;
                        nsources++;
                }
        }

        return nsources;
}


static int
afr_sh_mark_if_size_differs (afr_self_heal_t *sh, int child_count)
{
        int32_t ** pending_matrix;
        int i, j;

        int size_differs = 0;

        pending_matrix = sh->pending_matrix;

        for (i = 0; i < child_count; i++) {
                for (j = 0; j < child_count; j++) {
                        if (SIZE_DIFFERS (&sh->buf[i], &sh->buf[j])
                            && (pending_matrix[i][j] == 0)
                            && (pending_matrix[j][i] == 0)) {
                                
                                pending_matrix[i][j] = 1;
                                pending_matrix[j][i] = 1;

                                size_differs = 1;
                        }
                }
        }

        return size_differs;
}

        
static int
afr_sh_mark_biggest_fool_as_source (afr_self_heal_t *sh,
                                    afr_node_character *characters, 
                                    int child_count)
{
        int i = 0;
        int biggest = 0;
        
        for (i = 0; i < child_count; i++) {
                if (characters[i].type == AFR_NODE_FOOL) {
                        biggest = i;
                        break;
                }
        }

        for (i = 0; i < child_count; i++) {
                if (characters[i].type != AFR_NODE_FOOL)
                        continue;
                
                if (SIZE_GREATER (&sh->buf[i], &sh->buf[biggest])) {
                        biggest = i;
                }
        }

        sh->sources[biggest] = 1;

        return 1;
}


static int
afr_sh_mark_biggest_as_source (afr_self_heal_t *sh, int child_count)
{
        int biggest = 0;
        int i;

        for (i = 0; i < child_count; i++) {
                if (SIZE_GREATER (&sh->buf[i], &sh->buf[biggest])) {
                        biggest = i;
                }
        }

        sh->sources[biggest] = 1;

        return 1;
}


int
afr_sh_mark_sources (afr_self_heal_t *sh, int child_count,
                     afr_self_heal_type type)
{
	int i = 0;

        int32_t ** pending_matrix;
        int *      sources;

        int size_differs = 0;

        pending_matrix = sh->pending_matrix;
        sources        = sh->sources;

	int nsources = 0;

        /* stores the 'characters' (innocent, fool, wise) of the nodes */
        afr_node_character *
                characters = CALLOC (sizeof (afr_node_character), 
                                     child_count);

	/* start clean */
	for (i = 0; i < child_count; i++) {
		sources[i] = 0;
	}
        
        for (i = 0; i < child_count; i++) {
                if (afr_sh_is_innocent (pending_matrix[i], child_count)) {
                        characters[i].type = AFR_NODE_INNOCENT;

                } else if (afr_sh_is_fool (pending_matrix[i], i, child_count)) {
                        characters[i].type = AFR_NODE_FOOL;

                } else if (afr_sh_is_wise (pending_matrix[i], i, child_count)) {
                        characters[i].type = AFR_NODE_WISE;

                } else {
                        gf_log ("[module:replicate]", GF_LOG_ERROR,
                                "Could not determine the state of subvolume %d!"
                                " (This message should never appear."
                                " Please file a bug report to "
                                "<gluster-devel@nongnu.org>.)", i);
                }
        }

        if (type == AFR_SELF_HEAL_DATA) {
                size_differs = afr_sh_mark_if_size_differs (sh, child_count);
        }

        if (afr_sh_all_nodes_innocent (characters, child_count)) {
                if (size_differs) {
                        nsources = afr_sh_mark_biggest_as_source (sh,
                                                                  child_count);
                }

        } else if (afr_sh_wise_nodes_exist (characters, child_count)) {
                afr_sh_compute_wisdom (pending_matrix, characters, child_count);

                if (afr_sh_wise_nodes_conflict (characters, child_count)) {
                        /* split-brain */

                        nsources = -1;
                        goto out;

                } else {
                        nsources = afr_sh_mark_wisest_as_sources (sources, 
                                                                  characters,
                                                                  child_count);
                }
        } else {
                nsources = afr_sh_mark_biggest_fool_as_source (sh, characters,
                                                               child_count);
        }

out:
        FREE (characters);

	return nsources;
}


void
afr_sh_pending_to_delta (afr_private_t *priv, dict_t **xattr,
                         int32_t *delta_matrix[], int success[],
                         int child_count, afr_transaction_type type)
{
	int i = 0;
	int j = 0;
        int k = 0;

        int32_t * pending = NULL;
        int       ret     = 0;

	/* start clean */
	for (i = 0; i < child_count; i++) {
		for (j = 0; j < child_count; j++) {
			delta_matrix[i][j] = 0;
		}
	}

	for (i = 0; i < child_count; i++) {
                pending = NULL;

                for (j = 0; j < child_count; j++) {
                        ret = dict_get_ptr (xattr[i], priv->pending_key[j],
                                            VOID(&pending));
                        
                        if (!success[j])
                                continue;

                        k = afr_index_for_transaction_type (type);
                        
                        if (pending) {
                                delta_matrix[i][j] = -(ntoh32 (pending[k]));
                        } else {
                                delta_matrix[i][j]  = 0;
                        }

                }
	}
}


int
afr_sh_delta_to_xattr (afr_private_t *priv,
                       int32_t *delta_matrix[], dict_t *xattr[],
		       int child_count, afr_transaction_type type)
{
	int i = 0;
	int j = 0;
        int k = 0;

	int ret = 0;

	int32_t *pending = 0;

	for (i = 0; i < child_count; i++) {
		if (!xattr[i])
			continue;

		for (j = 0; j < child_count; j++) {
                        pending = CALLOC (sizeof (int32_t), 3);
                        /* 3 = data+metadata+entry */

                        k = afr_index_for_transaction_type (type);

			pending[k] = hton32 (delta_matrix[i][j]);

                        ret = dict_set_bin (xattr[i], priv->pending_key[j], 
                                            pending,
                                            3 * sizeof (int32_t));
		}
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
        int            j  = 0;

	priv = this->private;

        for (i = 0; i < priv->child_count; i++) {
                ret = dict_get_ptr (xattr, priv->pending_key[i],
                                    &tmp_pending);

                if (ret != 0)
                        return 0;
                
                pending = tmp_pending;

                j = afr_index_for_transaction_type (AFR_METADATA_TRANSACTION);

                if (pending[j])
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

	int           ret = -1;
	int            i  = 0;
        int            j  = 0;

	priv = this->private;

        for (i = 0; i < priv->child_count; i++) {
                ret = dict_get_ptr (xattr, priv->pending_key[i],
                                    &tmp_pending);

                if (ret != 0)
                        return 0;
                
                pending = tmp_pending;

                j = afr_index_for_transaction_type (AFR_DATA_TRANSACTION);

                if (pending[j])
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

	int           ret = -1;
	int            i  = 0;
        int            j  = 0;

	priv = this->private;

        for (i = 0; i < priv->child_count; i++) {
                ret = dict_get_ptr (xattr, priv->pending_key[i],
                                    &tmp_pending);

                if (ret != 0)
                        return 0;
                
                pending = tmp_pending;

                j = afr_index_for_transaction_type (AFR_ENTRY_TRANSACTION);

                if (pending[j])
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
                sh->locked_nodes[i] = 0;
        }

	for (i = 0; i < priv->child_count; i++) {
		if (sh->xattr[i])
			dict_unref (sh->xattr[i]);
		sh->xattr[i] = NULL;
	}

	if (local->govinda_gOvinda) {
		gf_log (this->name, GF_LOG_TRACE,
			"aborting selfheal of %s",
			local->loc.path);
		sh->completion_cbk (frame, this);
	} else {
		gf_log (this->name, GF_LOG_TRACE,
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

        for (i = 0; i < priv->child_count; i++) {
                if (sh->locked_nodes[i])
                        call_count++;
        }

        if (call_count == 0) {
                afr_sh_missing_entries_done (frame, this);
                return 0;
        }

	local->call_count = call_count;

	for (i = 0; i < priv->child_count; i++) {
		if (sh->locked_nodes[i]) {
			gf_log (this->name, GF_LOG_TRACE,
				"unlocking %"PRId64"/%s on subvolume %s",
				sh->parent_loc.inode->ino, local->loc.name,
				priv->children[i]->name);

			STACK_WIND (frame, sh_missing_entries_unlck_cbk,
				    priv->children[i],
				    priv->children[i]->fops->entrylk,
                                    this->name,
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
		int32_t op_ret, int op_errno,
                struct stat *preop, struct stat *postop)
{
        afr_local_t *local = NULL;

        int call_count = 0;

        local = frame->local;

        call_count = afr_frame_return (frame);
        
        if (call_count == 0) {
                STACK_DESTROY (frame->root);
        }
        
	return 0;
}


static int
sh_missing_entries_newentry_cbk (call_frame_t *frame, void *cookie,
				 xlator_t *this,
				 int32_t op_ret, int32_t op_errno,
				 inode_t *inode, struct stat *buf,
                                 struct stat *preparent,
                                 struct stat *postparent)
{
	afr_local_t     *local = NULL;
	afr_self_heal_t *sh = NULL;
	afr_private_t   *priv = NULL;
	call_frame_t    *setattr_frame = NULL;
	int              call_count = 0;
	int              child_index = 0;
        
	struct stat     stbuf;
        int32_t         valid = 0;

	local = frame->local;
	sh    = &local->self_heal;
	priv  = this->private;

	child_index = (long) cookie;

#ifdef HAVE_STRUCT_STAT_ST_ATIM_TV_NSEC
	stbuf.st_atim = sh->buf[sh->source].st_atim;
	stbuf.st_mtim = sh->buf[sh->source].st_mtim;
        
#elif HAVE_STRUCT_STAT_ST_ATIMESPEC_TV_NSEC
	stbuf.st_atimespec = sh->buf[sh->source].st_atimespec;
	stbuf.st_mtimespec = sh->buf[sh->source].st_mtimespec;
#else
	stbuf.st_atime = sh->buf[sh->source].st_atime;
	stbuf.st_mtime = sh->buf[sh->source].st_mtime;
#endif

        stbuf.st_uid = sh->buf[sh->source].st_uid;
        stbuf.st_gid = sh->buf[sh->source].st_gid;
        
        valid = GF_SET_ATTR_UID   | GF_SET_ATTR_GID |
                GF_SET_ATTR_ATIME | GF_SET_ATTR_MTIME;
        
	if (op_ret == 0) {
		setattr_frame = copy_frame (frame);
                
                setattr_frame->local = CALLOC (1, sizeof (afr_local_t));

                ((afr_local_t *)setattr_frame->local)->call_count = 1;

		gf_log (this->name, GF_LOG_TRACE,
			"setattr (%s) on subvolume %s",
			local->loc.path, priv->children[child_index]->name);

		STACK_WIND (setattr_frame, sh_destroy_cbk,
			    priv->children[child_index],
			    priv->children[child_index]->fops->setattr,
			    &local->loc, &stbuf, valid);
	}

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

	gf_log (this->name, GF_LOG_TRACE,
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

	gf_log (this->name, GF_LOG_TRACE,
		"mkdir %s mode 0%o on %d subvolumes",
		local->loc.path, st_mode, enoent_count);

	for (i = 0; i < priv->child_count; i++) {
		if (sh->child_errno[i] == ENOENT) {
                        if (!strcmp (local->loc.path, "/")) {
                                /* We shouldn't try to create "/" */

                                sh_missing_entries_finish (frame, this);

                                return 0;
                        } else {
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

	gf_log (this->name, GF_LOG_TRACE,
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
				 const char *link, struct stat *sbuf)
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
                               inode_t *inode, struct stat *buf, dict_t *xattr,
                               struct stat *postparent)
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
			gf_log (this->name, GF_LOG_TRACE,
				"path %s on subvolume %s is of mode 0%o",
				local->loc.path,
				priv->children[child_index]->name,
				buf->st_mode);

			local->self_heal.buf[child_index] = *buf;
		} else {
			gf_log (this->name, GF_LOG_TRACE,
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
	
	if (xattr_req) {
                for (i = 0; i < priv->child_count; i++) {
                        ret = dict_set_uint64 (xattr_req, 
                                               priv->pending_key[i],
                                               3 * sizeof(int32_t));
                }
        }

	for (i = 0; i < priv->child_count; i++) {
		if (local->child_up[i]) {
			gf_log (this->name, GF_LOG_TRACE,
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

                        sh->locked_nodes[child_index] = 0;
			gf_log (this->name, GF_LOG_DEBUG,
				"locking inode of %s on child %d failed: %s",
				local->loc.path, child_index,
				strerror (op_errno));
		} else {
                        sh->locked_nodes[child_index] = 1;
			gf_log (this->name, GF_LOG_TRACE,
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

	gf_log (this->name, GF_LOG_TRACE,
		"attempting to recreate missing entries for path=%s",
		local->loc.path);

	afr_build_parent_loc (&sh->parent_loc, &local->loc);

	call_count = local->child_count;

	local->call_count = call_count;

	for (i = 0; i < priv->child_count; i++) {
		if (local->child_up[i]) {
			STACK_WIND_COOKIE (frame, sh_missing_entries_lk_cbk,
                                           (void *) (long) i,
                                           priv->children[i],
                                           priv->children[i]->fops->entrylk,
                                           this->name,
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

	gf_log (this->name, GF_LOG_TRACE,
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
	sh->locked_nodes = CALLOC (sizeof (*sh->locked_nodes), priv->child_count);

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
		gf_log (this->name, GF_LOG_TRACE,
			"proceeding to metadata check on %s",
			local->loc.path);
		afr_sh_missing_entries_done (frame, this);
	}

	return 0;
}
