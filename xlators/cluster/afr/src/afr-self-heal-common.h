/*
  Copyright (c) 2008-2011 Gluster, Inc. <http://www.gluster.com>
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

#define FILE_HAS_HOLES(buf) (((buf)->ia_size) > ((buf)->ia_blocks * 512))

typedef enum {
        AFR_SELF_HEAL_ENTRY,
        AFR_SELF_HEAL_METADATA,
        AFR_SELF_HEAL_DATA,
        AFR_SELF_HEAL_INVALID = -1,
} afr_self_heal_type;

typedef enum {
        AFR_LOOKUP_FAIL_CONFLICTS = 1,
        AFR_LOOKUP_FAIL_MISSING_GFIDS = 2,
} afr_lookup_flags_t;

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
                         int32_t *delta_matrix[], unsigned char success[],
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

int
afr_build_sources (xlator_t *xlator, dict_t **xattr, struct iatt *bufs,
                   int32_t **pending_matrix, int32_t *sources,
                   int32_t *success_children, afr_transaction_type type);
void
afr_sh_common_reset (afr_self_heal_t *sh, unsigned int child_count);

void
afr_sh_common_lookup_resp_handler (call_frame_t *frame, void *cookie,
                                   xlator_t *this,
                                   int32_t op_ret, int32_t op_errno,
                                   inode_t *inode, struct iatt *buf,
                                   dict_t *xattr, struct iatt *postparent,
                                   loc_t *loc);

int
afr_sh_common_lookup (call_frame_t *frame, xlator_t *this, loc_t *loc,
                      afr_lookup_done_cbk_t lookup_cbk, uuid_t uuid,
                      int32_t flags);
int
afr_sh_entry_expunge_remove (call_frame_t *expunge_frame, xlator_t *this,
                             int active_src, struct iatt *buf);
int
afr_sh_entrylk (call_frame_t *frame, xlator_t *this, loc_t *loc,
                char *base_name, afr_lock_cbk_t lock_cbk);
int
afr_sh_entry_impunge_create (call_frame_t *impunge_frame, xlator_t *this,
                             int child_index, struct iatt *buf,
                             struct iatt *postparent);
int
afr_sh_data_unlock (call_frame_t *frame, xlator_t *this,
                    afr_lock_cbk_t lock_cbk);
afr_local_t *
afr_local_copy (afr_local_t *l, xlator_t *this);
int
afr_sh_data_lock (call_frame_t *frame, xlator_t *this,
                  off_t start, off_t len,
                  afr_lock_cbk_t success_handler,
                  afr_lock_cbk_t failure_handler);
void
afr_sh_set_error (afr_self_heal_t *sh, int32_t op_errno);
void
afr_sh_mark_source_sinks (call_frame_t *frame, xlator_t *this);
typedef int
(*afr_fxattrop_cbk_t) (call_frame_t *frame, void *cookie,
                       xlator_t *this, int32_t op_ret, int32_t op_errno,
                       dict_t *xattr);
int
afr_build_child_loc (xlator_t *this, loc_t *child, loc_t *parent, char *name);
int
afr_impunge_frame_create (call_frame_t *frame, xlator_t *this,
                          int active_source, int ret_child, mode_t entry_mode,
                          call_frame_t **impunge_frame);
#endif /* __AFR_SELF_HEAL_COMMON_H__ */
