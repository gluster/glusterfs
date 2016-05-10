/*
  Copyright (c) 2015 Red Hat, Inc. <http://www.redhat.com>
  This file is part of GlusterFS.

  This file is licensed to you under your choice of the GNU Lesser
  General Public License, version 3 or any later version (LGPLv3 or
  later), or the GNU General Public License, version 2 (GPLv2), in all
  cases as published by the Free Software Foundation.
*/

#ifndef _PC_MESSAGES_H__
#define _PC_MESSAGES_H__

#ifndef _CONFIG_H
#define _CONFIG_H
#include "config.h"
#endif

#include "glfs-message-id.h"

/*! \file client-messages.h
 *  \brief Protocol client log-message IDs and their descriptions
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
 *       - Retain macro naming as glfs_msg_X (for readability across developers)
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

#define GLFS_PC_BASE                GLFS_MSGID_COMP_PC
#define GLFS_PC_NUM_MESSAGES        66
#define GLFS_PC_MSGID_END           (GLFS_PC_BASE + GLFS_NUM_MESSAGES + 1)
/* Messages with message IDs */
#define glfs_msg_start_x GLFS_PC_BASE, "Invalid: Start of messages"
/*------------*/

#define PC_MSG_TIMER_EXPIRED                    (GLFS_PC_BASE + 1)

/*!
 * @messageid
 * @diagnosis
 * @recommendedaction
 *
 */

#define PC_MSG_DIR_OP_FAILED                    (GLFS_PC_BASE + 2)

/*!
 * @messageid
 * @diagnosis
 * @recommendedaction
 *
 */

#define PC_MSG_FILE_OP_FAILED                   (GLFS_PC_BASE + 3)

/*!
 * @messageid
 * @diagnosis
 * @recommendedaction
 *
 */

#define PC_MSG_TIMER_REG                        (GLFS_PC_BASE + 4)

/*!
 * @messageid
 * @diagnosis
 * @recommendedaction
 *
 */

#define PC_MSG_GRACE_TIMER_CANCELLED            (GLFS_PC_BASE + 5)

/*!
 * @messageid
 * @diagnosis
 * @recommendedaction
 *
 */

#define PC_MSG_DICT_SET_FAILED                  (GLFS_PC_BASE + 6)

/*!
 * @messageid
 * @diagnosis
 * @recommendedaction
 *
 */

#define PC_MSG_DICT_GET_FAILED                  (GLFS_PC_BASE + 7)

/*!
 * @messageid
 * @diagnosis
 * @recommendedaction
 *
 */

#define PC_MSG_NO_MEMORY                        (GLFS_PC_BASE + 8)

/*!
 * @messageid
 * @diagnosis
 * @recommendedaction
 *
 */

#define PC_MSG_RPC_CBK_FAILED                   (GLFS_PC_BASE + 9)

/*!
 * @messageid
 * @diagnosis
 * @recommendedaction
 *
 */

#define PC_MSG_FUNCTION_CALL_ERROR              (GLFS_PC_BASE + 10)

/*!
 * @messageid
 * @diagnosis
 * @recommendedaction
 *
 */

#define PC_MSG_RPC_INITED_ALREADY               (GLFS_PC_BASE + 11)

/*!
 * @messageid
 * @diagnosis
 * @recommendedaction
 *
 */

#define PC_MSG_RPC_INIT                         (GLFS_PC_BASE + 12)

/*!
 * @messageid
 * @diagnosis
 * @recommendedaction
 *
 */

#define PC_MSG_RPC_DESTROY                      (GLFS_PC_BASE + 13)

/*!
 * @messageid
 * @diagnosis
 * @recommendedaction
 *
 */

#define PC_MSG_RPC_INVALID_CALL                 (GLFS_PC_BASE + 14)

/*!
 * @messageid
 * @diagnosis
 * @recommendedaction
 *
 */

#define PC_MSG_INVALID_ENTRY                    (GLFS_PC_BASE + 15)

/*!
 * @messageid
 * @diagnosis
 * @recommendedaction
 *
 */

#define PC_MSG_HANDSHAKE_RETURN                 (GLFS_PC_BASE + 16)

/*!
 * @messageid
 * @diagnosis
 * @recommendedaction
 *
 */

#define PC_MSG_CHILD_UP_NOTIFY_FAILED           (GLFS_PC_BASE + 17)

/*!
 * @messageid
 * @diagnosis
 * @recommendedaction
 *
 */

#define PC_MSG_CLIENT_DISCONNECTED              (GLFS_PC_BASE + 18)

/*!
 * @messageid
 * @diagnosis
 * @recommendedaction
 *
 */

#define PC_MSG_CHILD_DOWN_NOTIFY_FAILED         (GLFS_PC_BASE + 19)

/*!
 * @messageid
 * @diagnosis
 * @recommendedaction
 *
 */

#define PC_MSG_PARENT_UP                        (GLFS_PC_BASE + 20)

/*!
 * @messageid
 * @diagnosis
 * @recommendedaction
 *
 */

#define PC_MSG_PARENT_DOWN                      (GLFS_PC_BASE + 21)

/*!
 * @messageid
 * @diagnosis
 * @recommendedaction
 *
 */

#define PC_MSG_RPC_INIT_FAILED                  (GLFS_PC_BASE + 22)

/*!
 * @messageid
 * @diagnosis
 * @recommendedaction
 *
 */

#define PC_MSG_RPC_NOTIFY_FAILED                (GLFS_PC_BASE + 23)

/*!
 * @messageid
 * @diagnosis
 * @recommendedaction
 *
 */

#define PC_MSG_FD_DUPLICATE_TRY                 (GLFS_PC_BASE + 24)

/*!
 * @messageid
 * @diagnosis
 * @recommendedaction
 *
 */

#define PC_MSG_FD_SET_FAIL                      (GLFS_PC_BASE + 25)

/*!
 * @messageid
 * @diagnosis
 * @recommendedaction
 *
 */

#define PC_MSG_DICT_UNSERIALIZE_FAIL            (GLFS_PC_BASE + 26)

/*!
 * @messageid
 * @diagnosis
 * @recommendedaction
 *
 */

#define PC_MSG_FD_GET_FAIL                      (GLFS_PC_BASE + 27)

/*!
 * @messageid
 * @diagnosis
 * @recommendedaction
 *
 */

#define PC_MSG_FD_CTX_INVALID                   (GLFS_PC_BASE + 28)

/*!
 * @messageid
 * @diagnosis
 * @recommendedaction
 *
 */

#define PC_MSG_FOP_SEND_FAILED                  (GLFS_PC_BASE + 29)

/*!
 * @messageid
 * @diagnosis
 * @recommendedaction
 *
 */

#define PC_MSG_XDR_DECODING_FAILED              (GLFS_PC_BASE + 30)

/*!
 * @messageid
 * @diagnosis
 * @recommendedaction
 *
 */

#define PC_MSG_REMOTE_OP_FAILED                 (GLFS_PC_BASE + 31)

/*!
 * @messageid
 * @diagnosis
 * @recommendedaction
 *
 */

#define PC_MSG_RPC_STATUS_ERROR                 (GLFS_PC_BASE + 32)

/*!
 * @messageid
 * @diagnosis
 * @recommendedaction
 *
 */

#define PC_MSG_VOL_FILE_NOT_FOUND               (GLFS_PC_BASE + 33)

/*!
 * @messageid
 * @diagnosis
 * @recommendedaction
 *
 */

#define PC_MSG_SEND_REQ_FAIL                    (GLFS_PC_BASE + 34)

/*!
 * @messageid
 * @diagnosis
 * @recommendedaction
 *
 */

#define PC_MSG_LOCK_VERSION_SERVER              (GLFS_PC_BASE + 35)

/*!
 * @messageid
 * @diagnosis
 * @recommendedaction
 *
 */

#define PC_MSG_SET_LK_VERSION_ERROR             (GLFS_PC_BASE + 36)

/*!
 * @messageid
 * @diagnosis
 * @recommendedaction
 *
 */

#define PC_MSG_LOCK_REQ_FAIL                    (GLFS_PC_BASE + 37)

/*!
 * @messageid
 * @diagnosis
 * @recommendedaction
 *
 */

#define PC_MSG_CLIENT_REQ_FAIL                  (GLFS_PC_BASE + 38)

/*!
 * @messageid
 * @diagnosis
 * @recommendedaction
 *
 */

#define PC_MSG_LOCK_ERROR                       (GLFS_PC_BASE + 39)

/*!
 * @messageid
 * @diagnosis
 * @recommendedaction
 *
 */

#define PC_MSG_LOCK_REACQUIRE                   (GLFS_PC_BASE + 40)

/*!
 * @messageid
 * @diagnosis
 * @recommendedaction
 *
 */

#define PC_MSG_CHILD_UP_NOTIFY                  (GLFS_PC_BASE + 41)

/*!
 * @messageid
 * @diagnosis
 * @recommendedaction
 *
 */

#define PC_MSG_CHILD_UP_NOTIFY_DELAY            (GLFS_PC_BASE + 42)

/*!
 * @messageid
 * @diagnosis
 * @recommendedaction
 *
 */

#define PC_MSG_VOL_SET_FAIL                     (GLFS_PC_BASE + 43)

/*!
 * @messageid
 * @diagnosis
 * @recommendedaction
 *
 */

#define PC_MSG_SETVOLUME_FAIL                   (GLFS_PC_BASE + 44)

/*!
 * @messageid
 * @diagnosis
 * @recommendedaction
 *
 */

#define PC_MSG_VOLFILE_NOTIFY_FAILED            (GLFS_PC_BASE + 45)

/*!
 * @messageid
 * @diagnosis
 * @recommendedaction
 *
 */

#define PC_MSG_REMOTE_VOL_CONNECTED             (GLFS_PC_BASE + 46)

/*!
 * @messageid
 * @diagnosis
 * @recommendedaction
 *
 */

#define PC_MSG_LOCK_MISMATCH                    (GLFS_PC_BASE + 47)

/*!
 * @messageid
 * @diagnosis
 * @recommendedaction
 *
 */

#define PC_MSG_LOCK_MATCH                       (GLFS_PC_BASE + 48)

/*!
 * @messageid
 * @diagnosis
 * @recommendedaction
 *
 */

#define PC_MSG_AUTH_FAILED                      (GLFS_PC_BASE + 49)

/*!
 * @messageid
 * @diagnosis
 * @recommendedaction
 *
 */

#define PC_MSG_AUTH_FAILED_NOTIFY_FAILED        (GLFS_PC_BASE + 50)

/*!
 * @messageid
 * @diagnosis
 * @recommendedaction
 *
 */

#define PC_MSG_CHILD_CONNECTING_EVENT           (GLFS_PC_BASE + 51)

/*!
 * @messageid
 * @diagnosis
 * @recommendedaction
 *
 */

#define PC_MSG_CHILD_CONNECTING_NOTIFY_FAILED   (GLFS_PC_BASE + 52)

/*!
 * @messageid
 * @diagnosis
 * @recommendedaction
 *
 */

#define PC_MSG_PROCESS_UUID_SET_FAIL            (GLFS_PC_BASE + 53)

/*!
 * @messageid
 * @diagnosis
 * @recommendedaction
 *
 */

#define PC_MSG_DICT_ERROR                       (GLFS_PC_BASE + 54)

/*!
 * @messageid
 * @diagnosis
 * @recommendedaction
 *
 */

#define PC_MSG_DICT_SERIALIZE_FAIL              (GLFS_PC_BASE + 55)

/*!
 * @messageid
 * @diagnosis
 * @recommendedaction
 *
 */

#define PC_MSG_PGM_NOT_FOUND                    (GLFS_PC_BASE + 56)

/*!
 * @messageid
 * @diagnosis
 * @recommendedaction
 *
 */

#define PC_MSG_VERSION_INFO                     (GLFS_PC_BASE + 57)

/*!
 * @messageid
 * @diagnosis
 * @recommendedaction
 *
 */

#define PC_MSG_PORT_NUM_ERROR                   (GLFS_PC_BASE + 58)

/*!
 * @messageid
 * @diagnosis
 * @recommendedaction
 *
 */

#define PC_MSG_VERSION_ERROR                    (GLFS_PC_BASE + 59)

/*!
 * @messageid
 * @diagnosis
 * @recommendedaction
 *
 */

#define PC_MSG_DIR_OP_SUCCESS                   (GLFS_PC_BASE + 60)

/*!
 * @messageid
 * @diagnosis
 * @recommendedaction
 *
 */

#define PC_MSG_BAD_FD                           (GLFS_PC_BASE + 61)

/*!
 * @messageid
 * @diagnosis
 * @recommendedaction
 *
 */

#define PC_MSG_CLIENT_LOCK_INFO                 (GLFS_PC_BASE + 62)

/*!
 * @messageid
 * @diagnosis
 * @recommendedaction
 *
 */

#define PC_MSG_CACHE_INVALIDATION_FAIL          (GLFS_PC_BASE + 63)

/*!
 * @messageid
 * @diagnosis
 * @recommendedaction
 *
 */

#define PC_MSG_CHILD_STATUS                     (GLFS_PC_BASE + 64)

/*!
 * @messageid
 * @diagnosis
 * @recommendedaction
 *
 */

#define PC_MSG_GFID_NULL                       (GLFS_PC_BASE + 65)

/*!
 * @messageid
 * @diagnosis
 * @recommendedaction
 *
 */

#define PC_MSG_RECALL_LEASE_FAIL                (GLFS_PC_BASE + 66)

/*!
 * @messageid
 * @diagnosis
 * @recommendedaction
 *
 */
/*------------*/

#define glfs_msg_end_x GLFS_MSGID_END, "Invalid: End of messages"

#endif /* !_PC_MESSAGES_H__ */
