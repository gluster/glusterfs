/*
 Copyright (c) 2015 Red Hat, Inc. <http://www.redhat.com>
 This file is part of GlusterFS.

 This file is licensed to you under your choice of the GNU Lesser
 General Public License, version 3 or any later version (LGPLv3 or
 later), or the GNU General Public License, version 2 (GPLv2), in all
 cases as published by the Free Software Foundation.
 */

#ifndef _BITROT_STUB_MESSAGES_H_
#define _BITROT_STUB_MESSAGES_H_

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

GLFS_MSGID(BITROT_STUB,
        BRS_MSG_NO_MEMORY,
        BRS_MSG_SET_EVENT_FAILED,
        BRS_MSG_MEM_ACNT_FAILED,
        BRS_MSG_CREATE_FRAME_FAILED,
        BRS_MSG_SET_CONTEXT_FAILED,
        BRS_MSG_CHANGE_VERSION_FAILED,
        BRS_MSG_ADD_FD_TO_LIST_FAILED,
        BRS_MSG_SET_FD_CONTEXT_FAILED,
        BRS_MSG_CREATE_ANONYMOUS_FD_FAILED,
        BRS_MSG_NO_CHILD,
        BRS_MSG_STUB_ALLOC_FAILED,
        BRS_MSG_GET_INODE_CONTEXT_FAILED,
        BRS_MSG_CANCEL_SIGN_THREAD_FAILED,
        BRS_MSG_ADD_FD_TO_INODE,
        BRS_MSG_SIGN_VERSION_ERROR,
        BRS_MSG_BAD_OBJ_MARK_FAIL,
        BRS_MSG_NON_SCRUB_BAD_OBJ_MARK,
        BRS_MSG_REMOVE_INTERNAL_XATTR,
        BRS_MSG_SET_INTERNAL_XATTR,
        BRS_MSG_BAD_OBJECT_ACCESS,
        BRS_MSG_BAD_CONTAINER_FAIL,
        BRS_MSG_BAD_OBJECT_DIR_FAIL,
        BRS_MSG_BAD_OBJECT_DIR_SEEK_FAIL,
        BRS_MSG_BAD_OBJECT_DIR_TELL_FAIL,
        BRS_MSG_BAD_OBJECT_DIR_READ_FAIL,
        BRS_MSG_GET_FD_CONTEXT_FAILED,
        BRS_MSG_BAD_HANDLE_DIR_NULL,
        BRS_MSG_BAD_OBJ_THREAD_FAIL,
        BRS_MSG_BAD_OBJ_DIR_CLOSE_FAIL,
        BRS_MSG_LINK_FAIL,
        BRS_MSG_BAD_OBJ_UNLINK_FAIL,
        BRS_MSG_DICT_SET_FAILED,
        BRS_MSG_PATH_GET_FAILED
);

#endif /* !_BITROT_STUB_MESSAGES_H_ */
