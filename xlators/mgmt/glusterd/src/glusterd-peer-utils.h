/*
   Copyright (c) 2014 Red Hat, Inc. <http://www.redhat.com>
   This file is part of GlusterFS.

   This file is licensed to you under your choice of the GNU Lesser
   General Public License, version 3 or any later version (LGPLv3 or
   later), or the GNU General Public License, version 2 (GPLv2), in all
   cases as published by the Free Software Foundation.
*/

#ifndef _GLUSTERD_PEER_UTILS_H
#define _GLUSTERD_PEER_UTILS_H

#include "glusterd.h"
#include "glusterd-utils.h"

int32_t
glusterd_friend_cleanup (glusterd_peerinfo_t *peerinfo);

int32_t
glusterd_peer_destroy (glusterd_peerinfo_t *peerinfo);

int
glusterd_friend_find_by_hostname (const char *hoststr,
                                  glusterd_peerinfo_t  **peerinfo);

int
glusterd_hostname_to_uuid (char *hostname, uuid_t uuid);

int
glusterd_friend_find_by_uuid (uuid_t uuid, glusterd_peerinfo_t  **peerinfo);

int
glusterd_peerinfo_new (glusterd_peerinfo_t **peerinfo,
                       glusterd_friend_sm_state_t state, uuid_t *uuid,
                       const char *hostname, int port);

gf_boolean_t
glusterd_peerinfo_is_uuid_unknown (glusterd_peerinfo_t *peerinfo);

gf_boolean_t
glusterd_chk_peers_connected_befriended (uuid_t skip_uuid);

char *
glusterd_uuid_to_hostname (uuid_t uuid);

char*
gd_peer_uuid_str (glusterd_peerinfo_t *peerinfo);

gf_boolean_t
glusterd_are_vol_all_peers_up (glusterd_volinfo_t *volinfo,
                               struct list_head *peers, char **down_peerstr);

int32_t
glusterd_peer_hostname_new (const char *hostname,
                            glusterd_peer_hostname_t **name);
void
glusterd_peer_hostname_free (glusterd_peer_hostname_t *name);

gf_boolean_t
gd_peer_has_address (glusterd_peerinfo_t *peerinfo, const char *address);

int
gd_add_address_to_peer (glusterd_peerinfo_t *peerinfo, const char *address);

int
gd_add_friend_to_dict (glusterd_peerinfo_t *friend, dict_t *dict,
                       const char *prefix);

int
gd_peerinfo_from_dict (dict_t *dict, char *prefix,
                       glusterd_peerinfo_t **peerinfo);
#endif /* _GLUSTERD_PEER_UTILS_H */
