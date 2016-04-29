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

/* file bit-rot-bitd-messages.h
 * brief BIT-ROT log-message IDs and their descriptions
 */

/* NOTE: Rules for message additions
 * 1) Each instance of a message is _better_ left with a unique message ID, even
 *    if the message format is the same. Reasoning is that, if the message
 *    format needs to change in one instance, the other instances are not
 *    impacted or the new change does not change the ID of the instance being
 *    modified.
 * 2) Addition of a message,
 *       - Should increment the GLFS_NUM_MESSAGES
 *       - Append to the list of messages defined, towards the end
 *       - Retain macro naming as glfs_msg_X (for redability across developers)
 * NOTE: Rules for message format modifications
 * 3) Check acorss the code if the message ID macro in question is reused
 *    anywhere. If reused then then the modifications should ensure correctness
 *    everywhere, or needs a new message ID as (1) above was not adhered to. If
 *    not used anywhere, proceed with the required modification.
 * NOTE: Rules for message deletion
 * 4) Check (3) and if used anywhere else, then cannot be deleted. If not used
 *    anywhere, then can be deleted, but will leave a hole by design, as
 *    addition rules specify modification to the end of the list and not filling
 *    holes.
 */

#define GLFS_BITROT_BITD_BASE                   GLFS_MSGID_COMP_BITROT_BITD
#define GLFS_BITROT_BITD_NUM_MESSAGES           55
#define GLFS_MSGID_END                          (GLFS_BITROT_BITD_BASE + \
                                           GLFS_BITROT_BITD_NUM_MESSAGES + 1)
/* Messaged with message IDs */
#define glfs_msg_start_x  GLFS_BITROT_BITD_BASE, "Invalid: Start of messages"
/*------------*/


#define BRB_MSG_FD_CREATE_FAILED               (GLFS_BITROT_BITD_BASE + 1)
/*!
 * @messageid
 * @diagnosis
 * @recommendedaction
 *
 */

#define BRB_MSG_READV_FAILED                (GLFS_BITROT_BITD_BASE + 2)
/*!
 * @messageid
 * @diagnosis
 * @recommendedaction
 *
 */

#define BRB_MSG_BLOCK_READ_FAILED           (GLFS_BITROT_BITD_BASE + 3)
/*!
 * @messageid
 * @diagnosis
 * @recommendedaction
 *
 */
#define BRB_MSG_CALC_CHECKSUM_FAILED        (GLFS_BITROT_BITD_BASE + 4)
/*!
 * @messageid
 * @diagnosis
 * @recommendedaction
 *
 */
#define BRB_MSG_NO_MEMORY                   (GLFS_BITROT_BITD_BASE + 5)
/*!
 * @messageid
 * @diagnosis
 * @recommendedaction
 *
 */
#define BRB_MSG_GET_SIGN_FAILED             (GLFS_BITROT_BITD_BASE + 6)
/*!
 * @messageid
 * @diagnosis
 * @recommendedaction
 *
 */
#define BRB_MSG_SET_SIGN_FAILED             (GLFS_BITROT_BITD_BASE + 7)
/*!
 * @messageid
 * @diagnosis
 * @recommendedaction
 *
 */
#define BRB_MSG_OP_FAILED                   (GLFS_BITROT_BITD_BASE + 8)
/*!
 * @messageid
 * @diagnosis
 * @recommendedaction
 *
 */
#define BRB_MSG_READ_AND_SIGN_FAILED        (GLFS_BITROT_BITD_BASE + 9)
/*!
 * @messageid
 * @diagnosis
 * @recommendedaction
 *
 */
#define BRB_MSG_SIGN_FAILED                 (GLFS_BITROT_BITD_BASE + 10)
/*!
 * @messageid
 * @diagnosis
 * @recommendedaction
 *
 */
#define BRB_MSG_GET_SUBVOL_FAILED           (GLFS_BITROT_BITD_BASE + 11)
/*!
 * @messageid
 * @diagnosis
 * @recommendedaction
 *
 */
#define BRB_MSG_SET_TIMER_FAILED            (GLFS_BITROT_BITD_BASE + 12)
/*!
 * @messageid
 * @diagnosis
 * @recommendedaction
 *
 */
#define BRB_MSG_GET_INFO_FAILED             (GLFS_BITROT_BITD_BASE + 13)
/*!
 * @messageid
 * @diagnosis
 * @recommendedaction
 *
 */
#define BRB_MSG_PATH_FAILED                 (GLFS_BITROT_BITD_BASE + 14)
/*!
 * @messageid
 * @diagnosis
 * @recommendedaction
 *
 */
#define BRB_MSG_MARK_BAD_FILE               (GLFS_BITROT_BITD_BASE + 15)
/*!
 * @messageid
 * @diagnosis
 * @recommendedaction
 *
 */
#define BRB_MSG_TRIGGER_SIGN                (GLFS_BITROT_BITD_BASE + 16)
/*!
 * @messageid
 * @diagnosis
 * @recommendedaction
 *
 */
#define BRB_MSG_REGISTER_FAILED             (GLFS_BITROT_BITD_BASE + 17)
/*!
 * @messageid
 * @diagnosis
 * @recommendedaction
 *
 */
#define BRB_MSG_CRAWLING_START              (GLFS_BITROT_BITD_BASE + 18)
/*!
 * @messageid
 * @diagnosis
 * @recommendedaction
 *
 */
#define BRB_MSG_SPAWN_FAILED                (GLFS_BITROT_BITD_BASE + 19)
/*!
 * @messageid
 * @diagnosis
 * @recommendedaction
 *
 */
#define BRB_MSG_INVALID_SUBVOL_CHILD        (GLFS_BITROT_BITD_BASE + 20)
/*!
 * @messageid
 * @diagnosis
 * @recommendedaction
 *
 */
#define BRB_MSG_SKIP_OBJECT                 (GLFS_BITROT_BITD_BASE + 21)
/*!
 * @messageid
 * @diagnosis
 * @recommendedaction
 *
 */
#define BRB_MSG_NO_CHILD                    (GLFS_BITROT_BITD_BASE + 22)
/*!
 * @messageid
 * @diagnosis
 * @recommendedaction
 *
 */
#define BRB_MSG_CHECKSUM_MISMATCH           (GLFS_BITROT_BITD_BASE + 23)
/*!
 * @messageid
 * @diagnosis
 * @recommendedaction
 *
 */
#define BRB_MSG_MARK_CORRUPTED              (GLFS_BITROT_BITD_BASE + 24)
/*!
 * @messageid
 * @diagnosis
 * @recommendedaction
 *
 */
#define BRB_MSG_CRAWLING_FINISH               (GLFS_BITROT_BITD_BASE + 25)
/*!
 * @messageid
 * @diagnosis
 * @recommendedaction
 *
 */
#define BRB_MSG_CALC_ERROR                  (GLFS_BITROT_BITD_BASE + 26)
/*!
 * @messageid
 * @diagnosis
 * @recommendedaction
 *
 */
#define BRB_MSG_LOOKUP_FAILED               (GLFS_BITROT_BITD_BASE + 27)
/*!
 * @messageid
 * @diagnosis
 * @recommendedaction
 *
 */
#define BRB_MSG_PARTIAL_VERSION_PRESENCE    (GLFS_BITROT_BITD_BASE + 28)
/*!
 * @messageid
 * @diagnosis
 * @recommendedaction
 *
 */
#define BRB_MSG_MEM_ACNT_FAILED             (GLFS_BITROT_BITD_BASE + 29)
/*!
 * @messageid
 * @diagnosis
 * @recommendedaction
 *
 */
#define BRB_MSG_TIMER_WHEEL_UNAVAILABLE     (GLFS_BITROT_BITD_BASE + 30)
/*!
 * @messageid
 * @diagnosis
 * @recommendedaction
 *
 */
#define BRB_MSG_BITROT_LOADED               (GLFS_BITROT_BITD_BASE + 31)
/*!
 * @messageid
 * @diagnosis
 * @recommendedaction
 *
 */
#define BRB_MSG_SCALE_DOWN_FAILED           (GLFS_BITROT_BITD_BASE + 32)
/*!
 * @messageid
 * @diagnosis
 * @recommendedaction
 *
 */
#define BRB_MSG_SCALE_UP_FAILED             (GLFS_BITROT_BITD_BASE + 33)
/*!
 * @messageid
 * @diagnosis
 * @recommendedaction
 *
 */
#define BRB_MSG_SCALE_DOWN_SCRUBBER         (GLFS_BITROT_BITD_BASE + 34)
/*!
 * @messageid
 * @diagnosis
 * @recommendedaction
 *
 */
#define BRB_MSG_SCALING_UP_SCRUBBER         (GLFS_BITROT_BITD_BASE + 35)
/*!
 * @messageid
 * @diagnosis
 * @recommendedaction
 */
#define BRB_MSG_UNKNOWN_THROTTLE            (GLFS_BITROT_BITD_BASE + 36)
/*!
 * @messageid
 * @diagnosis
 * @recommendedaction
 *
 */
#define BRB_MSG_RATE_LIMIT_INFO             (GLFS_BITROT_BITD_BASE + 37)
/*!
 * @messageid
 * @diagnosis
 * @recommendedaction
 *
 */
#define BRB_MSG_SCRUB_INFO                  (GLFS_BITROT_BITD_BASE + 38)
/*!
 * @messageid
 * @diagnosis
 * @recommendedaction
 *
 */
#define BRB_MSG_CONNECTED_TO_BRICK          (GLFS_BITROT_BITD_BASE + 39)
/*!
 * @messageid
 * @diagnosis
 * @recommendedaction
 *
 */
#define BRB_MSG_BRICK_INFO                  (GLFS_BITROT_BITD_BASE + 40)
/*!
 * @messageid
 * @diagnosis
 * @recommendedaction
 *
 */
#define BRB_MSG_SUBVOL_CONNECT_FAILED       (GLFS_BITROT_BITD_BASE + 41)
/*!
 * @messageid
 * @diagnosis
 * @recommendedaction
 *
 */
#define BRB_MSG_INVALID_SUBVOL              (GLFS_BITROT_BITD_BASE + 42)
/*!
 * @messageid
 * @diagnosis
 * @recommendedaction
 *
 */
#define BRB_MSG_RESCHEDULE_SCRUBBER_FAILED  (GLFS_BITROT_BITD_BASE + 43)
/*!
 * @messageid
 * @diagnosis
 * @recommendedaction
 *
 */

#define BRB_MSG_SCRUB_START                 (GLFS_BITROT_BITD_BASE + 44)
/*!
 * @messageid
 * @diagnosis
 * @recommendedaction
 *
 */
#define BRB_MSG_SCRUB_FINISH                (GLFS_BITROT_BITD_BASE + 45)
/*!
 * @messageid
 * @diagnosis
 * @recommendedaction
 *
 */
#define BRB_MSG_SCRUB_RUNNING               (GLFS_BITROT_BITD_BASE + 46)
/*!
 * @messageid
 * @diagnosis
 * @recommendedaction
 *
 */
#define BRB_MSG_SCRUB_RESCHEDULED           (GLFS_BITROT_BITD_BASE + 47)
/*!
 * @messageid
 * @diagnosis
 * @recommendedaction
 *
 */
#define BRB_MSG_SCRUB_TUNABLE              (GLFS_BITROT_BITD_BASE + 48)
/*!
 * @messageid
 * @diagnosis
 * @recommendedaction
 *
 */
/*------------*/
#define BRB_MSG_SCRUB_THREAD_CLEANUP       (GLFS_BITROT_BITD_BASE + 49)
/*!
 * @messageid
 * @diagnosis
 * @recommendedaction
 *
 */
/*------------*/
#define BRB_MSG_SCRUBBER_CLEANED           (GLFS_BITROT_BITD_BASE + 50)
/*!
 * @messageid
 * @diagnosis
 * @recommendedaction
 *
 */
/*------------*/
#define BRB_MSG_GENERIC_SSM_INFO           (GLFS_BITROT_BITD_BASE + 51)
/*!
 * @messageid
 * @diagnosis
 * @recommendedaction
 *
 */
/*------------*/
#define BRB_MSG_ZERO_TIMEOUT_BUG           (GLFS_BITROT_BITD_BASE + 52)
/*!
 * @messageid
 * @diagnosis
 * @recommendedaction
 *
 */
/*------------*/
#define BRB_MSG_BAD_OBJ_READDIR_FAIL       (GLFS_BITROT_BITD_BASE + 53)
/*!
 * @messageid
 * @diagnosis
 * @recommendedaction
 *
 */
/*------------*/
#define BRB_MSG_SSM_FAILED                 (GLFS_BITROT_BITD_BASE + 54)
/*!
 * @messageid
 * @diagnosis
 * @recommendedaction
 *
 */
/*------------*/
#define BRB_MSG_SCRUB_WAIT_FAILED          (GLFS_BITROT_BITD_BASE + 55)
/*!
 * @messageid
 * @diagnosis
 * @recommendedaction
 *
 */
/*------------*/

#define glfs_msg_end_x GLFS_MSGID_END, "Invalid: End of messages"
#endif /* !_BITROT_BITD_MESSAGES_H_ */
