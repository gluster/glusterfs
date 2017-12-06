/*
 Copyright (c) 2015 Red Hat, Inc. <http://www.redhat.com>
 This file is part of GlusterFS.

 This file is licensed to you under your choice of the GNU Lesser
 General Public License, version 3 or any later version (LGPLv3 or
 later), or the GNU General Public License, version 2 (GPLv2), in all
 cases as published by the Free Software Foundation.
 */

#ifndef _SHARD_MESSAGES_H_
#define _SHARD_MESSAGES_H_

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

GLFS_MSGID(SHARD,
        SHARD_MSG_BASE_FILE_LOOKUP_FAILED,
        SHARD_MSG_DICT_SET_FAILED,
        SHARD_MSG_DOT_SHARD_NODIR,
        SHARD_MSG_FD_CTX_SET_FAILED,
        SHARD_MSG_INODE_CTX_GET_FAILED,
        SHARD_MSG_INODE_CTX_SET_FAILED,
        SHARD_MSG_INODE_PATH_FAILED,
        SHARD_MSG_INTERNAL_XATTR_MISSING,
        SHARD_MSG_INVALID_VOLFILE,
        SHARD_MSG_LOOKUP_SHARD_FAILED,
        SHARD_MSG_MEM_ACCT_INIT_FAILED,
        SHARD_MSG_NULL_THIS,
        SHARD_MSG_SIZE_SET_FAILED,
        SHARD_MSG_STAT_FAILED,
        SHARD_MSG_TRUNCATE_LAST_SHARD_FAILED,
        SHARD_MSG_UPDATE_FILE_SIZE_FAILED,
        SHARD_MSG_FOP_NOT_SUPPORTED,
        SHARD_MSG_INVALID_FOP,
        SHARD_MSG_MEMALLOC_FAILED
);

#endif /* !_SHARD_MESSAGES_H_ */
