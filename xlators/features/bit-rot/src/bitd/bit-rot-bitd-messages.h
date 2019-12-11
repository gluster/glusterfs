/*
 Copyright (c) 2015 Red Hat, Inc. <http://www.redhat.com>
 This file is part of GlusterFS.

 This file is licensed to you under your choice of the GNU Lesser
 General Public License, version 3 or any later version (LGPLv3 or
 later), or the GNU General Public License, version 2 (GPLv2), in all
 cases as published by the Free Software Foundation.
 */

#ifndef _BITROT_BITD_MESSAGES_H_
#define _BITROT_BITD_MESSAGES_H_

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

GLFS_MSGID(BITROT_BITD, BRB_MSG_FD_CREATE_FAILED, BRB_MSG_READV_FAILED,
           BRB_MSG_BLOCK_READ_FAILED, BRB_MSG_CALC_CHECKSUM_FAILED,
           BRB_MSG_NO_MEMORY, BRB_MSG_GET_SIGN_FAILED, BRB_MSG_SET_SIGN_FAILED,
           BRB_MSG_OP_FAILED, BRB_MSG_READ_AND_SIGN_FAILED, BRB_MSG_SIGN_FAILED,
           BRB_MSG_GET_SUBVOL_FAILED, BRB_MSG_SET_TIMER_FAILED,
           BRB_MSG_GET_INFO_FAILED, BRB_MSG_PATH_FAILED, BRB_MSG_MARK_BAD_FILE,
           BRB_MSG_TRIGGER_SIGN, BRB_MSG_REGISTER_FAILED,
           BRB_MSG_CRAWLING_START, BRB_MSG_SPAWN_FAILED,
           BRB_MSG_INVALID_SUBVOL_CHILD, BRB_MSG_SKIP_OBJECT, BRB_MSG_NO_CHILD,
           BRB_MSG_CHECKSUM_MISMATCH, BRB_MSG_MARK_CORRUPTED,
           BRB_MSG_CRAWLING_FINISH, BRB_MSG_CALC_ERROR, BRB_MSG_LOOKUP_FAILED,
           BRB_MSG_PARTIAL_VERSION_PRESENCE, BRB_MSG_MEM_ACNT_FAILED,
           BRB_MSG_TIMER_WHEEL_UNAVAILABLE, BRB_MSG_BITROT_LOADED,
           BRB_MSG_SCALE_DOWN_FAILED, BRB_MSG_SCALE_UP_FAILED,
           BRB_MSG_SCALE_DOWN_SCRUBBER, BRB_MSG_SCALING_UP_SCRUBBER,
           BRB_MSG_UNKNOWN_THROTTLE, BRB_MSG_RATE_LIMIT_INFO,
           BRB_MSG_SCRUB_INFO, BRB_MSG_CONNECTED_TO_BRICK, BRB_MSG_BRICK_INFO,
           BRB_MSG_SUBVOL_CONNECT_FAILED, BRB_MSG_INVALID_SUBVOL,
           BRB_MSG_RESCHEDULE_SCRUBBER_FAILED, BRB_MSG_SCRUB_START,
           BRB_MSG_SCRUB_FINISH, BRB_MSG_SCRUB_RUNNING,
           BRB_MSG_SCRUB_RESCHEDULED, BRB_MSG_SCRUB_TUNABLE,
           BRB_MSG_SCRUB_THREAD_CLEANUP, BRB_MSG_SCRUBBER_CLEANED,
           BRB_MSG_GENERIC_SSM_INFO, BRB_MSG_ZERO_TIMEOUT_BUG,
           BRB_MSG_BAD_OBJ_READDIR_FAIL, BRB_MSG_SSM_FAILED,
           BRB_MSG_SCRUB_WAIT_FAILED, BRB_MSG_TRIGGER_SIGN_FAILED,
           BRB_MSG_EVENT_UNHANDLED, BRB_MSG_COULD_NOT_SCHEDULE_SCRUB,
           BRB_MSG_THREAD_CREATION_FAILED, BRB_MSG_MEM_POOL_ALLOC,
           BRB_MSG_SAVING_HASH_FAILED);

#define BRB_MSG_FD_CREATE_FAILED_STR "failed to create fd for the inode"
#define BRB_MSG_READV_FAILED_STR "readv failed"
#define BRB_MSG_BLOCK_READ_FAILED_STR "reading block failed"
#define BRB_MSG_NO_MEMORY_STR "failed to allocate memory"
#define BRB_MSG_CALC_CHECKSUM_FAILED_STR "calculating checksum failed"
#define BRB_MSG_GET_SIGN_FAILED_STR "failed to get the signature"
#define BRB_MSG_SET_SIGN_FAILED_STR "signing failed"
#define BRB_MSG_OP_FAILED_STR "failed on object"
#define BRB_MSG_TRIGGER_SIGN_FAILED_STR "Could not trigger signing"
#define BRB_MSG_READ_AND_SIGN_FAILED_STR "reading and signing of object failed"
#define BRB_MSG_SET_TIMER_FAILED_STR "Failed to allocate object expiry timer"
#define BRB_MSG_GET_SUBVOL_FAILED_STR                                          \
    "failed to get the subvolume for the brick"
#define BRB_MSG_PATH_FAILED_STR "path failed"
#define BRB_MSG_SKIP_OBJECT_STR "Entry is marked corrupted. skipping"
#define BRB_MSG_PARTIAL_VERSION_PRESENCE_STR                                   \
    "PArtial version xattr presence detected, ignoring"
#define BRB_MSG_TRIGGER_SIGN_STR "Triggering signing"
#define BRB_MSG_CRAWLING_START_STR                                             \
    "Crawling brick, scanning for unsigned objects"
#define BRB_MSG_CRAWLING_FINISH_STR "Completed crawling brick"
#define BRB_MSG_REGISTER_FAILED_STR "Register to changelog failed"
#define BRB_MSG_SPAWN_FAILED_STR "failed to spawn"
#define BRB_MSG_CONNECTED_TO_BRICK_STR "Connected to brick"
#define BRB_MSG_LOOKUP_FAILED_STR "lookup on root failed"
#define BRB_MSG_GET_INFO_FAILED_STR "failed to get stub info"
#define BRB_MSG_SCRUB_THREAD_CLEANUP_STR "Error cleaning up scanner thread"
#define BRB_MSG_SCRUBBER_CLEANED_STR "clened up scrubber for brick"
#define BRB_MSG_SUBVOL_CONNECT_FAILED_STR                                      \
    "callback handler for subvolume failed"
#define BRB_MSG_MEM_ACNT_FAILED_STR "Memory accounting init failed"
#define BRB_MSG_EVENT_UNHANDLED_STR "Event unhandled for child"
#define BRB_MSG_INVALID_SUBVOL_STR "Got event from invalid subvolume"
#define BRB_MSG_RESCHEDULE_SCRUBBER_FAILED_STR                                 \
    "on demand scrub schedule failed. Scrubber is not in pending state."
#define BRB_MSG_COULD_NOT_SCHEDULE_SCRUB_STR                                   \
    "Could not schedule ondemand scrubbing. Scrubbing will continue "          \
    "according to old frequency."
#define BRB_MSG_THREAD_CREATION_FAILED_STR "thread creation failed"
#define BRB_MSG_RATE_LIMIT_INFO_STR "Rate Limit Info"
#define BRB_MSG_MEM_POOL_ALLOC_STR "failed to allocate mem-pool for timer"
#define BRB_MSG_NO_CHILD_STR "FATAL: no children"
#define BRB_MSG_TIMER_WHEEL_UNAVAILABLE_STR "global timer wheel unavailable"
#define BRB_MSG_BITROT_LOADED_STR "bit-rot xlator loaded"
#define BRB_MSG_SAVING_HASH_FAILED_STR                                         \
    "failed to allocate memory for saving hash of the object"
#endif /* !_BITROT_BITD_MESSAGES_H_ */
