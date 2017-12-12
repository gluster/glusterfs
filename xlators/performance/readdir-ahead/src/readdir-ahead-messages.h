/*Copyright (c) 2015 Red Hat, Inc. <http://www.redhat.com>
  This file is part of GlusterFS.

  This file is licensed to you under your choice of the GNU Lesser
  General Public License, version 3 or any later version (LGPLv3 or
  later), or the GNU General Public License, version 2 (GPLv2), in all
  cases as published by the Free Software Foundation.
*/

#ifndef _READDIR_AHEAD_MESSAGES_H_
#define _READDIR_AHEAD_MESSAGES_H_

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

GLFS_MSGID(READDIR_AHEAD,
        READDIR_AHEAD_MSG_XLATOR_CHILD_MISCONFIGURED,
        READDIR_AHEAD_MSG_VOL_MISCONFIGURED,
        READDIR_AHEAD_MSG_NO_MEMORY,
        READDIR_AHEAD_MSG_DIR_RELEASE_PENDING_STUB,
        READDIR_AHEAD_MSG_OUT_OF_SEQUENCE,
        READDIR_AHEAD_MSG_DICT_OP_FAILED
);

#endif /* _READDIR_AHEAD_MESSAGES_H_ */
