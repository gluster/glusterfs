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
glusterd_peerinfo_cleanup (glusterd_peerinfo_t *peerinfo);

glusterd_peerinfo_t *
glusterd_peerinfo_find_by_hostname (const char *hoststr);

int
glusterd_hostname_to_uuid (char *hostname, uuid_t uuid);

glusterd_peerinfo_t *
glusterd_peerinfo_find_by_uuid (uuid_t uuid);

glusterd_peerinfo_t *
glusterd_peerinfo_find (uuid_t uuid, const char *hostname);

glusterd_peerinfo_t *
glusterd_peerinfo_new (glusterd_friend_sm_state_t state, uuid_t *uuid,
                       const char *hostname, int port);

gf_boolean_t
glusterd_chk_peers_connected_befriended (uuid_t skip_uuid);

char *
glusterd_uuid_to_hostname (uuid_t uuid);

char*
gd_peer_uuid_str (glusterd_peerinfo_t *peerinfo);

gf_boolean_t
glusterd_are_all_peers_up ();

gf_boolean_t
glusterd_are_vol_all_peers_up (glusterd_volinfo_t *volinfo,
                               struct cds_list_head *peers,
                               char **down_peerstr);

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

glusterd_peerinfo_t *
gd_peerinfo_find_from_hostname (const char *hoststr);

glusterd_peerinfo_t *
gd_peerinfo_find_from_addrinfo (const struct addrinfo *addr);

int
gd_update_peerinfo_from_dict (glusterd_peerinfo_t *peerinfo, dict_t *dict,
                              const char *prefix);

glusterd_peerinfo_t *
gd_peerinfo_from_dict (dict_t *dict, const char *prefix);

int
gd_add_peer_hostnames_to_dict (glusterd_peerinfo_t *peerinfo, dict_t *dict,
                               const char *prefix);
int
gd_add_peer_detail_to_dict (glusterd_peerinfo_t *peerinfo, dict_t *friends,
                            int count);
glusterd_peerinfo_t *
glusterd_peerinfo_find_by_generation (uint32_t generation);

int
glusterd_get_peers_count ();
#endif /* _GLUSTERD_PEER_UTILS_H */
