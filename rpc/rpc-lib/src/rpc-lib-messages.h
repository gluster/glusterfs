/*
  Copyright (c) 2015 Red Hat, Inc. <http://www.redhat.com>
  This file is part of GlusterFS.

  This file is licensed to you under your choice of the GNU Lesser
  General Public License, version 3 or any later version (LGPLv3 or
  later), or the GNU General Public License, version 2 (GPLv2), in all
  cases as published by the Free Software Foundation.
*/

#ifndef _RPC_LIB_MESSAGES_H_
#define _RPC_LIB_MESSAGES_H_

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

GLFS_MSGID(RPC_LIB,
        TRANS_MSG_ADDR_FAMILY_NOT_SPECIFIED,
        TRANS_MSG_UNKNOWN_ADDR_FAMILY,
        TRANS_MSG_REMOTE_HOST_ERROR,
        TRANS_MSG_DNS_RESOL_FAILED,
        TRANS_MSG_LISTEN_PATH_ERROR,
        TRANS_MSG_CONNECT_PATH_ERROR,
        TRANS_MSG_GET_ADDR_INFO_FAILED,
        TRANS_MSG_PORT_BIND_FAILED,
        TRANS_MSG_INET_ERROR,
        TRANS_MSG_GET_NAME_INFO_FAILED,
        TRANS_MSG_TRANSPORT_ERROR,
        TRANS_MSG_TIMEOUT_EXCEEDED,
        TRANS_MSG_SOCKET_BIND_ERROR
);

#endif /* !_RPC_LIB_MESSAGES_H_ */

