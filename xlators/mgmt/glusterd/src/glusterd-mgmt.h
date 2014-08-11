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

#ifndef _CONFIG_H
#define _CONFIG_H
#include "config.h"
#endif

void gd_mgmt_v3_collate_errors (struct syncargs *args, int op_ret, int op_errno,
                                char *op_errstr, int op_code,
                                glusterd_peerinfo_t *peerinfo, u_char *uuid);

int32_t
gd_mgmt_v3_pre_validate_fn (glusterd_op_t op, dict_t *dict,
                           char **op_errstr, dict_t *rsp_dict);

int32_t
gd_mgmt_v3_brick_op_fn (glusterd_op_t op, dict_t *dict,
                       char **op_errstr, dict_t *rsp_dict);

int32_t
gd_mgmt_v3_commit_fn (glusterd_op_t op, dict_t *dict,
                     char **op_errstr, dict_t *rsp_dict);

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
#endif /* _GLUSTERD_MGMT_H_ */
