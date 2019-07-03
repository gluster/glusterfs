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

GLFS_MSGID(
    CHANGELOG_LIB, CHANGELOG_LIB_MSG_OPEN_FAILED,
    CHANGELOG_LIB_MSG_FAILED_TO_RMDIR,
    CHANGELOG_LIB_MSG_SCRATCH_DIR_ENTRIES_CREATION_ERROR,
    CHANGELOG_LIB_MSG_THREAD_CREATION_FAILED, CHANGELOG_LIB_MSG_OPENDIR_ERROR,
    CHANGELOG_LIB_MSG_RENAME_FAILED, CHANGELOG_LIB_MSG_READ_ERROR,
    CHANGELOG_LIB_MSG_HTIME_ERROR, CHANGELOG_LIB_MSG_GET_TIME_ERROR,
    CHANGELOG_LIB_MSG_WRITE_FAILED, CHANGELOG_LIB_MSG_PTHREAD_ERROR,
    CHANGELOG_LIB_MSG_MMAP_FAILED, CHANGELOG_LIB_MSG_MUNMAP_FAILED,
    CHANGELOG_LIB_MSG_ASCII_ERROR, CHANGELOG_LIB_MSG_STAT_FAILED,
    CHANGELOG_LIB_MSG_GET_XATTR_FAILED, CHANGELOG_LIB_MSG_PUBLISH_ERROR,
    CHANGELOG_LIB_MSG_PARSE_ERROR, CHANGELOG_LIB_MSG_MIN_MAX_INFO,
    CHANGELOG_LIB_MSG_CLEANUP_ERROR, CHANGELOG_LIB_MSG_UNLINK_FAILED,
    CHANGELOG_LIB_MSG_NOTIFY_REGISTER_FAILED,
    CHANGELOG_LIB_MSG_INVOKE_RPC_FAILED, CHANGELOG_LIB_MSG_DRAINING_EVENT_INFO,
    CHANGELOG_LIB_MSG_CLEANING_BRICK_ENTRY_INFO,
    CHANGELOG_LIB_MSG_FREEING_ENTRY_INFO, CHANGELOG_LIB_MSG_XDR_DECODING_FAILED,
    CHANGELOG_LIB_MSG_NOTIFY_REGISTER_INFO,
    CHANGELOG_LIB_MSG_THREAD_CLEANUP_WARNING,
    CHANGELOG_LIB_MSG_COPY_FROM_BUFFER_FAILED,
    CHANGELOG_LIB_MSG_PTHREAD_JOIN_FAILED, CHANGELOG_LIB_MSG_HIST_FAILED,
    CHANGELOG_LIB_MSG_DRAINED_EVENT_INFO, CHANGELOG_LIB_MSG_PARSE_ERROR_CEASED,
    CHANGELOG_LIB_MSG_REQUESTING_INFO, CHANGELOG_LIB_MSG_FINAL_INFO);

#define CHANGELOG_LIB_MSG_NOTIFY_REGISTER_INFO_STR "Registering brick"
#define CHANGELOG_LIB_MSG_RENAME_FAILED_STR "error moving changelog file"
#define CHANGELOG_LIB_MSG_OPEN_FAILED_STR "cannot open changelog file"
#define CHANGELOG_LIB_MSG_UNLINK_FAILED_STR "failed to unlink"
#define CHANGELOG_LIB_MSG_FAILED_TO_RMDIR_STR "failed to rmdir"
#define CHANGELOG_LIB_MSG_STAT_FAILED_STR "stat failed on changelog file"
#define CHANGELOG_LIB_MSG_PARSE_ERROR_STR "could not parse changelog"
#define CHANGELOG_LIB_MSG_PARSE_ERROR_CEASED_STR                               \
    "parsing error, ceased publishing..."
#define CHANGELOG_LIB_MSG_HTIME_ERROR_STR "fop failed on htime file"
#define CHANGELOG_LIB_MSG_GET_XATTR_FAILED_STR                                 \
    "error extracting max timstamp from htime file"
#define CHANGELOG_LIB_MSG_MIN_MAX_INFO_STR "changelogs min max"
#define CHANGELOG_LIB_MSG_REQUESTING_INFO_STR "Requesting historical changelogs"
#define CHANGELOG_LIB_MSG_FINAL_INFO_STR "FINAL"
#define CHANGELOG_LIB_MSG_HIST_FAILED_STR                                      \
    "Requested changelog range is not available"
#define CHANGELOG_LIB_MSG_GET_TIME_ERROR_STR "wrong result"
#define CHANGELOG_LIB_MSG_CLEANING_BRICK_ENTRY_INFO_STR                        \
    "Cleaning brick entry for brick"
#define CHANGELOG_LIB_MSG_DRAINING_EVENT_INFO_STR "Draining event"
#define CHANGELOG_LIB_MSG_DRAINED_EVENT_INFO_STR "Drained event"
#define CHANGELOG_LIB_MSG_FREEING_ENTRY_INFO_STR "freeing entry"

#endif /* !_CHANGELOG_MESSAGES_H_ */
