/*
  Copyright (c) 2008-2012 Red Hat, Inc. <http://www.redhat.com>
  This file is part of GlusterFS.

  This file is licensed to you under your choice of the GNU Lesser
  General Public License, version 3 or any later version (LGPLv3 or
  later), or the GNU General Public License, version 2 (GPLv2), in all
  cases as published by the Free Software Foundation.
*/

#ifndef __AFR_SELF_HEAL_COMMON_H__
#define __AFR_SELF_HEAL_COMMON_H__

#define FILE_HAS_HOLES(buf) (((buf)->ia_size) > ((buf)->ia_blocks * 512))
#define AFR_SH_MIN_PARTICIPANTS 2

typedef enum {
        AFR_LOOKUP_FAIL_CONFLICTS = 1,
        AFR_LOOKUP_FAIL_MISSING_GFIDS = 2,
} afr_lookup_flags_t;

int
afr_sh_select_source (int sources[], int child_count);

int
afr_sh_source_count (int sources[], int child_count);

void
afr_sh_print_pending_matrix (int32_t *pending_matrix[], xlator_t *this);

void
afr_sh_print_split_brain_log (int32_t *pending_matrix[], xlator_t *this,
                              const char *loc);

int
afr_build_pending_matrix (char **pending_key, int32_t **pending_matrix,
                          unsigned char *ignorant_subvols,
                          dict_t *xattr[], afr_transaction_type type,
                          size_t child_count);

void
afr_sh_pending_to_delta (afr_private_t *priv, dict_t **xattr,
                         int32_t *delta_matrix[], unsigned char success[],
                         int child_count, afr_transaction_type type);

int
afr_mark_sources (xlator_t *this, int32_t *sources, int32_t **pending_matrix,
                  struct iatt *bufs, afr_self_heal_type type,
                  int32_t *success_children, int32_t *subvol_status);

int
afr_sh_delta_to_xattr (xlator_t *this,
                       int32_t *delta_matrix[], dict_t *xattr[],
		       int child_count, afr_transaction_type type);

void
afr_self_heal_type_str_get (afr_self_heal_t *self_heal_p, char *str,
                            size_t size);

afr_self_heal_type
afr_self_heal_type_for_transaction (afr_transaction_type type);

int
afr_build_sources (xlator_t *this, dict_t **xattr, struct iatt *bufs,
                   int32_t **pending_matrix, int32_t *sources,
                   int32_t *success_children, afr_transaction_type type,
                   int32_t *subvol_status, gf_boolean_t ignore_ignorant);
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
                      int32_t flags, dict_t *xdata);
int
afr_sh_entry_expunge_remove (call_frame_t *expunge_frame, xlator_t *this,
                             int active_src, struct iatt *buf,
                             struct iatt *parentbuf);
int
afr_sh_entrylk (call_frame_t *frame, xlator_t *this, loc_t *loc,
                char *base_name, afr_lock_cbk_t lock_cbk);
int
afr_sh_entry_impunge_create (call_frame_t *impunge_frame, xlator_t *this,
                             int child_index);
int
afr_sh_data_unlock (call_frame_t *frame, xlator_t *this, char *dom,
                    afr_lock_cbk_t lock_cbk);
afr_local_t *
afr_self_heal_local_init (afr_local_t *l, xlator_t *this);
int
afr_sh_data_lock (call_frame_t *frame, xlator_t *this,
                  off_t start, off_t len, gf_boolean_t block, char *dom,
                  afr_lock_cbk_t success_handler,
                  afr_lock_cbk_t failure_handler);
void
afr_sh_set_error (afr_self_heal_t *sh, int32_t op_errno);
void
afr_sh_mark_source_sinks (call_frame_t *frame, xlator_t *this);
typedef int
(*afr_fxattrop_cbk_t) (call_frame_t *frame, void *cookie,
                       xlator_t *this, int32_t op_ret, int32_t op_errno,
                       dict_t *xattr, dict_t *xdata);
int
afr_build_child_loc (xlator_t *this, loc_t *child, loc_t *parent, char *name);
int
afr_impunge_frame_create (call_frame_t *frame, xlator_t *this,
                          int active_source, call_frame_t **impunge_frame);
void
afr_sh_reset (call_frame_t *frame, xlator_t *this);

void
afr_children_intersection_get (int32_t *set1, int32_t *set2,
                               int *intersection, unsigned int child_count);
int
afr_get_no_xattr_dir_read_child (xlator_t *this, int32_t *success_children,
                                 struct iatt *bufs);
int
afr_sh_erase_pending (call_frame_t *frame, xlator_t *this,
                      afr_transaction_type type, afr_fxattrop_cbk_t cbk,
                      int (*finish)(call_frame_t *frame, xlator_t *this));

void
afr_set_local_for_unhealable (afr_local_t *local);

int
is_self_heal_failed (afr_self_heal_t *sh, afr_sh_fail_check_type type);

void
afr_set_self_heal_status (afr_self_heal_t *sh, afr_self_heal_status status);

void
afr_log_self_heal_completion_status (afr_local_t *local, gf_loglevel_t  logl);

char*
afr_get_pending_matrix_str (int32_t *pending_matrix[], xlator_t *this);
#endif /* __AFR_SELF_HEAL_COMMON_H__ */
