/*
 Copyright (c) 2015-2016 Red Hat, Inc. <http://www.redhat.com>
 This file is part of GlusterFS.

 This file is licensed to you under your choice of the GNU Lesser
 General Public License, version 3 or any later version (LGPLv3 or
 later), or the GNU General Public License, version 2 (GPLv2), in all
 cases as published by the Free Software Foundation.
 */

#ifndef _LEASES_MESSAGES_H_
#define _LEASES_MESSAGES_H_

#include "glfs-message-id.h"

/* To add new message IDs, append new identifiers at the end of the list.
 *
 * Never remove a message ID. If it's not used anymore, you can rename it or
 * leave it as it is, but not delete it. This is to prevent reutilization of
 * IDs by other messages.
 *
 * The component name must match one of the entries defined in
 * glfs-message-id.h.
 */

GLFS_MSGID(LEASES,
        LEASE_MSG_NO_MEM,
        LEASE_MSG_RECALL_FAIL,
        LEASE_MSG_INVAL_LEASE_ID,
        LEASE_MSG_INVAL_UNLK_LEASE,
        LEASE_MSG_INVAL_INODE_CTX,
        LEASE_MSG_NOT_ENABLED,
        LEASE_MSG_NO_TIMER_WHEEL,
        LEASE_MSG_CLNT_NOTFOUND,
        LEASE_MSG_INODE_NOTFOUND,
        LEASE_MSG_INVAL_FD_CTX,
        LEASE_MSG_INVAL_LEASE_TYPE
);

#endif /* !_LEASES_MESSAGES_H_ */
