/*
  Copyright (c) 2008-2010 Gluster, Inc. <http://www.gluster.com>
  This file is part of GlusterFS.

  GlusterFS is free software; you can redistribute it and/or modify
  it under the terms of the GNU Affero General Public License as published
  by the Free Software Foundation; either version 3 of the License,
  or (at your option) any later version.

  GlusterFS is distributed in the hope that it will be useful, but
  WITHOUT ANY WARRANTY; without even the implied warranty of
  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
  Affero General Public License for more details.

  You should have received a copy of the GNU Affero General Public License
  along with this program.  If not, see
  <http://www.gnu.org/licenses/>.
*/

#ifndef __AFR_SELF_HEAL_COMMON_H__
#define __AFR_SELF_HEAL_COMMON_H__

#define FILE_HAS_HOLES(buf) (((buf)->ia_size) > ((buf)->ia_blocks * 512))

typedef enum {
        AFR_SELF_HEAL_ENTRY,
        AFR_SELF_HEAL_METADATA,
        AFR_SELF_HEAL_DATA,
        AFR_SELF_HEAL_INVALID = -1,
} afr_self_heal_type;

int
afr_sh_select_source (int sources[], int child_count);

int
afr_sh_sink_count (int sources[], int child_count);

int
afr_sh_source_count (int sources[], int child_count);

void
afr_sh_print_pending_matrix (int32_t *pending_matrix[], xlator_t *this);

int
afr_build_pending_matrix (char **pending_key, int32_t **pending_matrix,
                          dict_t *xattr[], afr_transaction_type type,
                          size_t child_count);

void
afr_sh_pending_to_delta (afr_private_t *priv, dict_t **xattr,
                         int32_t *delta_matrix[], int success[],
                         int child_count, afr_transaction_type type);

int
afr_mark_sources (int32_t *sources, int32_t **pending_matrix, struct iatt *bufs,
                  int32_t child_count, afr_self_heal_type type,
                  int32_t *valid_children, const char *xlator_name);

int
afr_sh_delta_to_xattr (afr_private_t *priv,
                       int32_t *delta_matrix[], dict_t *xattr[],
		       int child_count, afr_transaction_type type);

int
afr_sh_is_matrix_zero (int32_t *pending_matrix[], int child_count);

void
afr_self_heal_type_str_get (afr_self_heal_t *self_heal_p, char *str,
                            size_t size);

afr_self_heal_type
afr_self_heal_type_for_transaction (afr_transaction_type type);

#endif /* __AFR_SELF_HEAL_COMMON_H__ */
