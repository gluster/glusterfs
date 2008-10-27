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
afr_self_heal_create_missing_entries (call_frame_t *frame, xlator_t *this)
{
	afr_local_t     *local = NULL;
	afr_self_heal_t *sh = NULL;
	afr_private_t   *priv = NULL;


	local = frame->local;
	sh = &local->self_heal;
	priv = this->private;

	build_parent_loc (&sh->parent_loc, &local->loc);

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

	if (local->success_count && local->enoent_count)
		afr_self_heal_create_missing_entries (frame, this);
	else
		afr_self_heal_metadata (frame, this);

	return 0;
}
