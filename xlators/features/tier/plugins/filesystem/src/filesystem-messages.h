/*
 Copyright (c) 2021 Pavilion Data Systems, Inc. <https://pavilion.io>
 This file is part of GlusterFS.

 This file is licensed to you under your choice of the GNU Lesser
 General Public License, version 3 or any later version (LGPLv3 or
 later), or the GNU General Public License, version 2 (GPLv2), in all
 cases as published by the Free Software Foundation.
 */

#ifndef _TIERFS_MESSAGES_H_
#define _TIERFS_MESSAGES_H_

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

GLFS_MSGID(TIERFS, TIERFS_EXTRACTION_FAILED, TIERFS_FREE,
           TIERFS_RESOURCE_ALLOCATION_FAILED, TIERFS_RESTORE_FAILED,
           TIERFS_READ_FAILED, TIERFS_NO_MEMORY, TIERFS_DLOPEN_FAILED);

#endif /* !_TIERFS_MESSAGES_H_ */
