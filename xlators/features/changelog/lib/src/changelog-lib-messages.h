/*
 Copyright (c) 2015 Red Hat, Inc. <http://www.redhat.com>
 This file is part of GlusterFS.

 This file is licensed to you under your choice of the GNU Lesser
 General Public License, version 3 or any later version (LGPLv3 or
 later), or the GNU General Public License, version 2 (GPLv2), in all
 cases as published by the Free Software Foundation.
 */

#ifndef _CHANGELOG_LIB_MESSAGES_H_
#define _CHANGELOG_LIB_MESSAGES_H_

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

GLFS_MSGID(CHANGELOG_LIB,
        CHANGELOG_LIB_MSG_OPEN_FAILED,
        CHANGELOG_LIB_MSG_FAILED_TO_RMDIR,
        CHANGELOG_LIB_MSG_SCRATCH_DIR_ENTRIES_CREATION_ERROR,
        CHANGELOG_LIB_MSG_THREAD_CREATION_FAILED,
        CHANGELOG_LIB_MSG_OPENDIR_ERROR,
        CHANGELOG_LIB_MSG_RENAME_FAILED,
        CHANGELOG_LIB_MSG_READ_ERROR,
        CHANGELOG_LIB_MSG_HTIME_ERROR,
        CHANGELOG_LIB_MSG_GET_TIME_ERROR,
        CHANGELOG_LIB_MSG_WRITE_FAILED,
        CHANGELOG_LIB_MSG_PTHREAD_ERROR,
        CHANGELOG_LIB_MSG_MMAP_FAILED,
        CHANGELOG_LIB_MSG_MUNMAP_FAILED,
        CHANGELOG_LIB_MSG_ASCII_ERROR,
        CHANGELOG_LIB_MSG_STAT_FAILED,
        CHANGELOG_LIB_MSG_GET_XATTR_FAILED,
        CHANGELOG_LIB_MSG_PUBLISH_ERROR,
        CHANGELOG_LIB_MSG_PARSE_ERROR,
        CHANGELOG_LIB_MSG_TOTAL_LOG_INFO,
        CHANGELOG_LIB_MSG_CLEANUP_ERROR,
        CHANGELOG_LIB_MSG_UNLINK_FAILED,
        CHANGELOG_LIB_MSG_NOTIFY_REGISTER_FAILED,
        CHANGELOG_LIB_MSG_INVOKE_RPC_FAILED,
        CHANGELOG_LIB_MSG_DRAINING_EVENT_INFO,
        CHANGELOG_LIB_MSG_CLEANING_BRICK_ENTRY_INFO,
        CHANGELOG_LIB_MSG_FREEING_ENTRY_INFO,
        CHANGELOG_LIB_MSG_XDR_DECODING_FAILED,
        CHANGELOG_LIB_MSG_NOTIFY_REGISTER_INFO,
        CHANGELOG_LIB_MSG_THREAD_CLEANUP_WARNING,
        CHANGELOG_LIB_MSG_COPY_FROM_BUFFER_FAILED,
        CHANGELOG_LIB_MSG_PTHREAD_JOIN_FAILED,
        CHANGELOG_LIB_MSG_HIST_FAILED
);

#endif /* !_CHANGELOG_MESSAGES_H_ */
