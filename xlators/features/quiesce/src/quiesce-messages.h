/*
 *   Copyright (c) 2016 Red Hat, Inc. <http://www.redhat.com>
 *   This file is part of GlusterFS.
 *
 *   This file is licensed to you under your choice of the GNU Lesser
 *   General Public License, version 3 or any later version (LGPLv3 or
 *   later), or the GNU General Public License, version 2 (GPLv2), in all
 *   cases as published by the Free Software Foundation.
 */

#ifndef __QUIESCE_MESSAGES_H__
#define __QUIESCE_MESSAGES_H__

#include <glusterfs/glfs-message-id.h>

/* To add new message IDs, append new identifiers at the end of the list.
 *
 * Never remove a message ID. If it's not used anymore, you can rename it or
 * leave it as it is, but not delete it. This is to prevent reutilization of
 * IDs by other messages.
 *
 * The component name must match one of the entries defined in
 * glfs-message-id.h.
 */

// clang-format off

GLFS_COMPONENT(QUIESCE);

GLFS_NEW(QUIESCE, QUIESCE_MSG_INVAL_HOST, "Invalid internet address", 1,
    GLFS_STR(address)
)

GLFS_NEW(QUIESCE, QUIESCE_MSG_FAILOVER_FAILED, "Failed to initiate failover", 2,
    GLFS_STR(host),
    GLFS_ERR(error)
)

// clang-format on

#endif /* __NL_CACHE_MESSAGES_H__ */
