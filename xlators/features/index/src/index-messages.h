/*
 Copyright (c) 2016 Red Hat, Inc. <http://www.redhat.com>
 This file is part of GlusterFS.

 This file is licensed to you under your choice of the GNU Lesser
 General Public License, version 3 or any later version (LGPLv3 or
 later), or the GNU General Public License, version 2 (GPLv2), in all
 cases as published by the Free Software Foundation.
 */

#ifndef _INDEX_MESSAGES_H_
#define _INDEX_MESSAGES_H_

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

GLFS_MSGID(INDEX,
        INDEX_MSG_INDEX_DIR_CREATE_FAILED,
        INDEX_MSG_INDEX_READDIR_FAILED,
        INDEX_MSG_INDEX_ADD_FAILED,
        INDEX_MSG_INDEX_DEL_FAILED,
        INDEX_MSG_DICT_SET_FAILED,
        INDEX_MSG_INODE_CTX_GET_SET_FAILED,
        INDEX_MSG_INVALID_ARGS,
        INDEX_MSG_FD_OP_FAILED,
        INDEX_MSG_WORKER_THREAD_CREATE_FAILED,
        INDEX_MSG_INVALID_GRAPH
);

#endif /* !_INDEX_MESSAGES_H_ */
