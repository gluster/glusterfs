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

#ifndef _CONFIG_H
#define _CONFIG_H
#include "config.h"
#endif

#define GLUSTERD_QUORUM_COUNT(peerinfo, inquorum_count,\
                              active_count, _exit)\
do {\
                if (peerinfo->quorum_contrib == QUORUM_WAITING)\
                        goto _exit;\
                if (_is_contributing_to_quorum (peerinfo->quorum_contrib))\
                        inquorum_count = inquorum_count + 1;\
                if (active_count && (peerinfo->quorum_contrib == QUORUM_UP))\
                        *active_count = *active_count + 1;\
} while (0)


int
glusterd_validate_quorum (xlator_t *this, glusterd_op_t op, dict_t *dict,
                          char **op_errstr);

gf_boolean_t
glusterd_is_quorum_changed (dict_t *options, char *option, char *value);

int
glusterd_do_quorum_action ();

int
glusterd_get_quorum_cluster_counts (xlator_t *this, int *active_count,
                                    int *quorum_count,
                                    struct cds_list_head *peer_list,
                                    gf_boolean_t _local__xaction_peers);

gf_boolean_t
glusterd_is_quorum_option (char *option);

gf_boolean_t
glusterd_is_volume_in_server_quorum (glusterd_volinfo_t *volinfo);

gf_boolean_t
glusterd_is_any_volume_in_server_quorum (xlator_t *this);

gf_boolean_t
does_gd_meet_server_quorum (xlator_t *this,
                            struct cds_list_head *peers_list,
                            gf_boolean_t _local__xaction_peers);
#endif /* _GLUSTERD_SERVER_QUORUM_H */
