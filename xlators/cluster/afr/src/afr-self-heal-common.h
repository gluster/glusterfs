/*
  Copyright (c) 2008-2009 Z RESEARCH, Inc. <http://www.zresearch.com>
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

#ifndef __AFR_SELF_HEAL_COMMON_H__
#define __AFR_SELF_HEAL_COMMON_H__

#define FILE_HAS_HOLES(buf) (((buf)->st_size) > ((buf)->st_blocks * 512))

typedef enum {
        AFR_SELF_HEAL_ENTRY,
        AFR_SELF_HEAL_METADATA,
        AFR_SELF_HEAL_DATA,
} afr_self_heal_type;

int
afr_sh_select_source (int sources[], int child_count);

int
afr_sh_sink_count (int sources[], int child_count);

int
afr_sh_source_count (int sources[], int child_count);

int
afr_sh_supress_errenous_children (int sources[], int child_errno[],
				  int child_count);

void
afr_sh_print_pending_matrix (int32_t *pending_matrix[], xlator_t *this);

void
afr_sh_build_pending_matrix (afr_private_t *priv,
                             int32_t *pending_matrix[], dict_t *xattr[],
			     int child_count, afr_transaction_type type);

void
afr_sh_pending_to_delta (afr_private_t *priv, dict_t **xattr,
                         int32_t *delta_matrix[], int success[],
                         int child_count, afr_transaction_type type);

int
afr_sh_mark_sources (afr_self_heal_t *sh, int child_count,
                     afr_self_heal_type type);

int
afr_sh_delta_to_xattr (afr_private_t *priv,
                       int32_t *delta_matrix[], dict_t *xattr[],
		       int child_count, afr_transaction_type type);

int
afr_sh_is_matrix_zero (int32_t *pending_matrix[], int child_count);


#endif /* __AFR_SELF_HEAL_COMMON_H__ */
