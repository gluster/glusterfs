/*
  Copyright (c) 2013 Red Hat, Inc. <http://www.redhat.com>
  This file is part of GlusterFS.

  This file is licensed to you under your choice of the GNU Lesser
  General Public License, version 3 or any later version (LGPLv3 or
  later), or the GNU General Public License, version 2 (GPLv2), in all
  cases as published by the Free Software Foundation.
*/

#ifndef _QUOTA_MESSAGES_H_
#define _QUOTA_MESSAGES_H_

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

GLFS_MSGID(QUOTA,
        Q_MSG_ENFORCEMENT_FAILED,
        Q_MSG_ENOMEM,
        Q_MSG_PARENT_NULL,
        Q_MSG_CROSSED_SOFT_LIMIT,
        Q_MSG_QUOTA_ENFORCER_RPC_INIT_FAILED,
        Q_MSG_REMOTE_OPERATION_FAILED,
        Q_MSG_FAILED_TO_SEND_FOP,
        Q_MSG_INVALID_VOLFILE,
        Q_MSG_INODE_PARENT_NOT_FOUND,
        Q_MSG_XDR_DECODE_ERROR,
        Q_MSG_DICT_UNSERIALIZE_FAIL,
        Q_MSG_DICT_SERIALIZE_FAIL,
        Q_MSG_RPCSVC_INIT_FAILED,
        Q_MSG_RPCSVC_LISTENER_CREATION_FAILED,
        Q_MSG_RPCSVC_REGISTER_FAILED,
        Q_MSG_XDR_DECODING_FAILED,
        Q_MSG_RPCCLNT_REGISTER_NOTIFY_FAILED,
        Q_MSG_ANCESTRY_BUILD_FAILED,
        Q_MSG_SIZE_KEY_MISSING,
        Q_MSG_INODE_CTX_GET_FAILED,
        Q_MSG_INODE_CTX_SET_FAILED,
        Q_MSG_LOOKUP_FAILED,
        Q_MSG_RPC_SUBMIT_FAILED,
        Q_MSG_ENFORCEMENT_SKIPPED,
        Q_MSG_INTERNAL_FOP_KEY_MISSING
);

#endif /* !_QUOTA_MESSAGES_H_ */

