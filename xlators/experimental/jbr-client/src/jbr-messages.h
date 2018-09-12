/*
  Copyright (c) 2015 Red Hat, Inc. <http://www.redhat.com>
  This file is part of GlusterFS.

  This file is licensed to you under your choice of the GNU Lesser
  General Public License, version 3 or any later version (LGPLv3 or
  later), or the GNU General Public License, version 2 (GPLv2), in all
  cases as published by the Free Software Foundation.
*/

#ifndef _JBR_MESSAGES_H_
#define _JBR_MESSAGES_H_

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

GLFS_MSGID(JBR, J_MSG_INIT_FAIL, J_MSG_RETRY_MSG, J_MSG_MEM_ERR, J_MSG_DICT_FLR,
           J_MSG_GENERIC, J_MSG_INVALID, J_MSG_NO_DATA, J_MSG_SYS_CALL_FAILURE,
           J_MSG_QUORUM_NOT_MET, J_MSG_LOCK_FAILURE);

#endif /* _JBR_MESSAGES_H_ */
