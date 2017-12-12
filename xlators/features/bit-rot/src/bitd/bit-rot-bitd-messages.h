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

GLFS_MSGID(BITROT_BITD,
        BRB_MSG_FD_CREATE_FAILED,
        BRB_MSG_READV_FAILED,
        BRB_MSG_BLOCK_READ_FAILED,
        BRB_MSG_CALC_CHECKSUM_FAILED,
        BRB_MSG_NO_MEMORY,
        BRB_MSG_GET_SIGN_FAILED,
        BRB_MSG_SET_SIGN_FAILED,
        BRB_MSG_OP_FAILED,
        BRB_MSG_READ_AND_SIGN_FAILED,
        BRB_MSG_SIGN_FAILED,
        BRB_MSG_GET_SUBVOL_FAILED,
        BRB_MSG_SET_TIMER_FAILED,
        BRB_MSG_GET_INFO_FAILED,
        BRB_MSG_PATH_FAILED,
        BRB_MSG_MARK_BAD_FILE,
        BRB_MSG_TRIGGER_SIGN,
        BRB_MSG_REGISTER_FAILED,
        BRB_MSG_CRAWLING_START,
        BRB_MSG_SPAWN_FAILED,
        BRB_MSG_INVALID_SUBVOL_CHILD,
        BRB_MSG_SKIP_OBJECT,
        BRB_MSG_NO_CHILD,
        BRB_MSG_CHECKSUM_MISMATCH,
        BRB_MSG_MARK_CORRUPTED,
        BRB_MSG_CRAWLING_FINISH,
        BRB_MSG_CALC_ERROR,
        BRB_MSG_LOOKUP_FAILED,
        BRB_MSG_PARTIAL_VERSION_PRESENCE,
        BRB_MSG_MEM_ACNT_FAILED,
        BRB_MSG_TIMER_WHEEL_UNAVAILABLE,
        BRB_MSG_BITROT_LOADED,
        BRB_MSG_SCALE_DOWN_FAILED,
        BRB_MSG_SCALE_UP_FAILED,
        BRB_MSG_SCALE_DOWN_SCRUBBER,
        BRB_MSG_SCALING_UP_SCRUBBER,
        BRB_MSG_UNKNOWN_THROTTLE,
        BRB_MSG_RATE_LIMIT_INFO,
        BRB_MSG_SCRUB_INFO,
        BRB_MSG_CONNECTED_TO_BRICK,
        BRB_MSG_BRICK_INFO,
        BRB_MSG_SUBVOL_CONNECT_FAILED,
        BRB_MSG_INVALID_SUBVOL,
        BRB_MSG_RESCHEDULE_SCRUBBER_FAILED,
        BRB_MSG_SCRUB_START,
        BRB_MSG_SCRUB_FINISH,
        BRB_MSG_SCRUB_RUNNING,
        BRB_MSG_SCRUB_RESCHEDULED,
        BRB_MSG_SCRUB_TUNABLE,
        BRB_MSG_SCRUB_THREAD_CLEANUP,
        BRB_MSG_SCRUBBER_CLEANED,
        BRB_MSG_GENERIC_SSM_INFO,
        BRB_MSG_ZERO_TIMEOUT_BUG,
        BRB_MSG_BAD_OBJ_READDIR_FAIL,
        BRB_MSG_SSM_FAILED,
        BRB_MSG_SCRUB_WAIT_FAILED
);

#endif /* !_BITROT_BITD_MESSAGES_H_ */
