/*
   Copyright (c) 2013-2014 Red Hat, Inc. <http://www.redhat.com>
   This file is part of GlusterFS.

   This file is licensed to you under your choice of the GNU Lesser
   General Public License, version 3 or any later version (LGPLv3 or
   later), or the GNU General Public License, version 2 (GPLv2), in all
   cases as published by the Free Software Foundation.
*/
#ifndef _GLUSTERD_MGMT_H_
#define _GLUSTERD_MGMT_H_

void gd_mgmt_v3_collate_errors (struct syncargs *args, int op_ret, int op_errno,
                                char *op_errstr, int op_code, uuid_t peerid,
                                u_char *uuid);

int32_t
gd_mgmt_v3_pre_validate_fn (glusterd_op_t op, dict_t *dict,
                            char **op_errstr, dict_t *rsp_dict,
                            uint32_t *op_errno);

int32_t
gd_mgmt_v3_brick_op_fn (glusterd_op_t op, dict_t *dict,
                       char **op_errstr, dict_t *rsp_dict);

int32_t
gd_mgmt_v3_commit_fn (glusterd_op_t op, dict_t *dict,
                      char **op_errstr, uint32_t *op_errno,
                      dict_t *rsp_dict);

int32_t
gd_mgmt_v3_post_validate_fn (glusterd_op_t op, int32_t op_ret, dict_t *dict,
                            char **op_errstr, dict_t *rsp_dict);

int32_t
glusterd_mgmt_v3_initiate_all_phases (rpcsvc_request_t *req, glusterd_op_t op,
                                     dict_t *dict);

int32_t
glusterd_mgmt_v3_initiate_snap_phases (rpcsvc_request_t *req, glusterd_op_t op,
                                      dict_t *dict);

int
glusterd_snap_pre_validate_use_rsp_dict (dict_t *dst, dict_t *src);

int32_t
glusterd_set_barrier_value (dict_t *dict, char *option);
int

glusterd_mgmt_v3_initiate_lockdown (glusterd_op_t op, dict_t *dict,
                                    char **op_errstr, uint32_t *op_errno,
                                    gf_boolean_t  *is_acquired,
                                    uint32_t txn_generation);

int
glusterd_mgmt_v3_build_payload (dict_t **req, char **op_errstr, dict_t *dict,
                                glusterd_op_t op);

int
glusterd_mgmt_v3_pre_validate (glusterd_op_t op, dict_t *req_dict,
                               char **op_errstr, uint32_t *op_errno,
                               uint32_t txn_generation);

int
glusterd_mgmt_v3_commit (glusterd_op_t op, dict_t *op_ctx, dict_t *req_dict,
                         char **op_errstr, uint32_t *op_errno,
                         uint32_t txn_generation);

int
glusterd_mgmt_v3_release_peer_locks (glusterd_op_t op, dict_t *dict,
                                     int32_t op_ret, char **op_errstr,
                                     gf_boolean_t  is_acquired,
                                     uint32_t txn_generation);

int32_t
glusterd_multiple_mgmt_v3_unlock (dict_t *dict, uuid_t uuid);

int
glusterd_reset_brick_prevalidate (dict_t *dict, char **op_errstr,
                                  dict_t *rsp_dict);
int
glusterd_op_reset_brick (dict_t *dict, dict_t *rsp_dict);
#endif /* _GLUSTERD_MGMT_H_ */
