/*Copyright (c) 2015 Red Hat, Inc. <http://www.redhat.com>
  This file is part of GlusterFS.

  This file is licensed to you under your choice of the GNU Lesser
  General Public License, version 3 or any later version (LGPLv3 or
  later), or the GNU General Public License, version 2 (GPLv2), in all
  cases as published by the Free Software Foundation.
*/

#ifndef _FS_CACHE_MESSAGES_H_
#define _FS_CACHE_MESSAGES_H_

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

GLFS_MSGID(FS_CACHE, FS_CACHE_MSG_ENFORCEMENT_FAILED,
           FS_CACHE_MSG_ERROR,
           FS_CACHE_MSG_WARNING,
           FS_CACHE_MSG_DEBUG,
           FS_CACHE_MSG_INFO,
           FS_CACHE_MSG_TRACE,
           FS_CACHE_MSG_VOL_MISCONFIGURED, FS_CACHE_MSG_INODE_NULL,
           FS_CACHE_MSG_PAGE_WAIT_VALIDATE, FS_CACHE_MSG_STR_COVERSION_FAILED,
           FS_CACHE_MSG_WASTED_COPY);

#endif /* _FS_CACHE_MESSAGES_H_ */
