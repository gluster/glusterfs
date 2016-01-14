/*
   Copyright (c) 2015 Red Hat, Inc. <http://www.redhat.com>
   This file is part of GlusterFS.

   This file is licensed to you under your choice of the GNU Lesser
   General Public License, version 3 or any later version (LGPLv3 or
   later), or the GNU General Public License, version 2 (GPLv2), in all
   cases as published by the Free Software Foundation.
*/
#ifndef _GLUSTERD_SERVER_QUORUM_H
#define _GLUSTERD_SERVER_QUORUM_H

int
glusterd_validate_quorum (xlator_t *this, glusterd_op_t op, dict_t *dict,
                          char **op_errstr);

gf_boolean_t
glusterd_is_quorum_changed (dict_t *options, char *option, char *value);

int
glusterd_do_quorum_action ();

int
glusterd_get_quorum_cluster_counts (xlator_t *this, int *active_count,
                                    int *quorum_count);

gf_boolean_t
glusterd_is_quorum_option (char *option);

gf_boolean_t
glusterd_is_volume_in_server_quorum (glusterd_volinfo_t *volinfo);

gf_boolean_t
glusterd_is_any_volume_in_server_quorum (xlator_t *this);

gf_boolean_t
does_gd_meet_server_quorum (xlator_t *this);

int
check_quorum_for_brick_start (glusterd_volinfo_t *volinfo,
                              gf_boolean_t node_quorum);

gf_boolean_t
does_quorum_meet (int active_count, int quorum_count);

#endif /* _GLUSTERD_SERVER_QUORUM_H */
