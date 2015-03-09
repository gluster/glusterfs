/*
  Copyright (c) 2013 Red Hat, Inc. <http://www.redhat.com>
  This file is part of GlusterFS.

  This file is licensed to you under your choice of the GNU Lesser
  General Public License, version 3 or any later version (LGPLv3 or
  later), or the GNU General Public License, version 2 (GPLv2), in all
  cases as published by the Free Software Foundation.
*/

#ifndef _PC_MESSAGES_H__
#define _PC_MESSAGES_H_

#ifndef _CONFIG_H
#define _CONFIG_H
#include "config.h"
#endif

#include "glfs-message-id.h"

/*! \file client-messages.h
 *  \brief Glusterd log-message IDs and their descriptions
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

#define GLFS_PC_BASE                GLFS_MSGID_COMP_PC
#define GLFS_NUM_MESSAGES           60
#define GLFS_MSGID_END              (GLFS_PC_BASE + GLFS_NUM_MESSAGES + 1)
/* Messages with message IDs */
#define glfs_msg_start_x GLFS_PC_BASE, "Invalid: Start of messages"
/*------------*/

#define PC_MSG_TIMER_EXPIRED                    (GLFS_PC_BASE + 1)
#define PC_MSG_DIR_OP_FAILED                    (GLFS_PC_BASE + 2)
#define PC_MSG_FILE_OP_FAILED                   (GLFS_PC_BASE + 3)
#define PC_MSG_TIMER_REG                        (GLFS_PC_BASE + 4)
#define PC_MSG_GRACE_TIMER_CANCELLED            (GLFS_PC_BASE + 5)
#define PC_MSG_DICT_SET_FAILED                  (GLFS_PC_BASE + 6)
#define PC_MSG_DICT_GET_FAILED                  (GLFS_PC_BASE + 7)
#define PC_MSG_NO_MEMORY                        (GLFS_PC_BASE + 8)
#define PC_MSG_RPC_CBK_FAILED                   (GLFS_PC_BASE + 9)
#define PC_MSG_RPC_NOT_FOUND                    (GLFS_PC_BASE + 10)
#define PC_MSG_RPC_INITED_ALREADY               (GLFS_PC_BASE + 11)
#define PC_MSG_RPC_INIT                         (GLFS_PC_BASE + 12)
#define PC_MSG_DESTROY                          (GLFS_PC_BASE + 13)
#define PC_MSG_RPC_INVALID_CALL                 (GLFS_PC_BASE + 14)
#define PC_MSG_INVALID_ENTRY                    (GLFS_PC_BASE + 15)
#define PC_MSG_HANDSHAKE_RETURN                 (GLFS_PC_BASE + 16)
#define PC_MSG_CHILD_UP_NOTIFY_FAILED           (GLFS_PC_BASE + 17)
#define PC_MSG_CLIENT_DISCONNECTED              (GLFS_PC_BASE + 18)
#define PC_MSG_CHILD_DOWN_NOTIFY_FAILED         (GLFS_PC_BASE + 19)
#define PC_MSG_PARENT_UP                        (GLFS_PC_BASE + 20)
#define PC_MSG_PARENT_DOWN                      (GLFS_PC_BASE + 21)
#define PC_MSG_RPC_INIT_FAILED                  (GLFS_PC_BASE + 22)
#define PC_MSG_RPC_NOTIFY_FAILED                (GLFS_PC_BASE + 23)
#define PC_MSG_FD_DUPLICATE_TRY                 (GLFS_PC_BASE + 24)
#define PC_MSG_FD_SET_FAIL                      (GLFS_PC_BASE + 25)
#define PC_MSG_DICT_UNSERIALIZE_FAIL            (GLFS_PC_BASE + 26)
#define PC_MSG_FD_GET_FAIL                      (GLFS_PC_BASE + 27)
#define PC_MSG_FD_CTX_INVALID                   (GLFS_PC_BASE + 28)
#define PC_MSG_FOP_SEND_FAILED                  (GLFS_PC_BASE + 29)
#define PC_MSG_XDR_DECODING_FAILED              (GLFS_PC_BASE + 30)
#define PC_MSG_REMOTE_OP_FAILED                 (GLFS_PC_BASE + 31)
#define PC_MSG_RPC_STATUS_ERROR                 (GLFS_PC_BASE + 32)
#define PC_MSG_VOL_FILE_NOT_FOUND               (GLFS_PC_BASE + 33)
#define PC_MSG_SEND_REQ_FAIL                    (GLFS_PC_BASE + 34)
#define PC_MSG_LOCK_VERSION_SERVER              (GLFS_PC_BASE + 35)
#define PC_MSG_SET_LK_VERSION_ERROR             (GLFS_PC_BASE + 36)
#define PC_MSG_LOCK_REQ_FAIL                    (GLFS_PC_BASE + 37)
#define PC_MSG_CLIENT_REQ_FAIL                  (GLFS_PC_BASE + 38)
#define PC_MSG_LOCK_ERROR                       (GLFS_PC_BASE + 39)
#define PC_MSG_LOCK_REACQUIRE                   (GLFS_PC_BASE + 40)
#define PC_MSG_CHILD_UP_NOTIFY                  (GLFS_PC_BASE + 41)
#define PC_MSG_CHILD_UP_NOTIFY_DELAY            (GLFS_PC_BASE + 42)
#define PC_MSG_VOL_SET_FAIL                     (GLFS_PC_BASE + 43)
#define PC_MSG_SETVOLUME_FAIL                   (GLFS_PC_BASE + 44)
#define PC_MSG_VOLFILE_NOTIFY_FAILED            (GLFS_PC_BASE + 45)
#define PC_MSG_REMOTE_VOL_CONNECTED             (GLFS_PC_BASE + 46)
#define PC_MSG_LOCK_MISMATCH                    (GLFS_PC_BASE + 47)
#define PC_MSG_LOCK_MATCH                       (GLFS_PC_BASE + 48)
#define PC_MSG_AUTH_FAILED                      (GLFS_PC_BASE + 49)
#define PC_MSG_AUTH_FAILED_NOTIFY_FAILED        (GLFS_PC_BASE + 50)
#define PC_MSG_CHILD_CONNECTING_EVENT           (GLFS_PC_BASE + 51)
#define PC_MSG_CHILD_CONNECTING_NOTIFY_FAILED   (GLFS_PC_BASE + 52)
#define PC_MSG_PROCESS_UUID_SET_FAIL            (GLFS_PC_BASE + 53)
#define PC_MSG_DICT_ERROR                       (GLFS_PC_BASE + 54)
#define PC_MSG_DICT_SERIALIZE_FAIL              (GLFS_PC_BASE + 55)
#define PC_MSG_PGM_NOT_FOUND                    (GLFS_PC_BASE + 56)
#define PC_MSG_VERSION_INFO                     (GLFS_PC_BASE + 57)
#define PC_MSG_PORT_NUM_ERROR                   (GLFS_PC_BASE + 58)
#define PC_MSG_VERSION_ERROR                    (GLFS_PC_BASE + 59)
#define PC_MSG_DIR_OP_SUCCESS                   (GLFS_PC_BASE + 60)
#define PC_MSG_FUNCTION_CALL_ERROR              (GLFS_PC_BASE + 61)

/*------------*/
#define glfs_msg_end_x GLFS_MSGID_END, "Invalid: End of messages"

#endif /* !_PC_MESSAGES_H_ */
