/*Copyright (c) 2015 Red Hat, Inc. <http://www.redhat.com>
  This file is part of GlusterFS.

  This file is licensed to you under your choice of the GNU Lesser
  General Public License, version 3 or any later version (LGPLv3 or
  later), or the GNU General Public License, version 2 (GPLv2), in all
  cases as published by the Free Software Foundation.
*/

#ifndef _QUICK_READ_MESSAGES_H_
#define _QUICK_READ_MESSAGES_H_

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

GLFS_MSGID(QUICK_READ,
        QUICK_READ_MSG_ENFORCEMENT_FAILED,
        QUICK_READ_MSG_INVALID_ARGUMENT,
        QUICK_READ_MSG_XLATOR_CHILD_MISCONFIGURED,
        QUICK_READ_MSG_NO_MEMORY,
        QUICK_READ_MSG_VOL_MISCONFIGURED,
        QUICK_READ_MSG_DICT_SET_FAILED,
        QUICK_READ_MSG_INVALID_CONFIG,
        QUICK_READ_MSG_LRU_NOT_EMPTY
);

#endif /* _QUICK_READ_MESSAGES_H_ */
