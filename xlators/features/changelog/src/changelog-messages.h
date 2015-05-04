/*
 Copyright (c) 2015 Red Hat, Inc. <http://www.redhat.com>
 This file is part of GlusterFS.

 This file is licensed to you under your choice of the GNU Lesser
 General Public License, version 3 or any later version (LGPLv3 or
 later), or the GNU General Public License, version 2 (GPLv2), in all
 cases as published by the Free Software Foundation.
 */

#ifndef _CHANGELOG_MESSAGES_H_
#define _CHANGELOG_MESSAGES_H_

#ifndef _CONFIG_H
#define _CONFIG_H
#include "config.h"
#endif

#include "glfs-message-id.h"

/*! \file changelog-messages.h
 *  \brief CHANGELOG log-message IDs and their descriptions.
 */

/* NOTE: Rules for message additions
 * 1) Each instance of a message is _better_ left with a unique message ID, even
 * if the message format is the same. Reasoning is that, if the message
 * format needs to change in one instance, the other instances are not
 * impacted or the new change does not change the ID of the instance being
 * modified.
 * 2) Addition of a message,
 * - Should increment the GLFS_NUM_MESSAGES
 * - Append to the list of messages defined, towards the end
 * - Retain macro naming as glfs_msg_X (for readability across developers)
 * NOTE: Rules for message format modifications
 * 3) Check acorss the code if the message ID macro in question is reused
 * anywhere. If reused then then the modifications should ensure correctness
 * everywhere, or needs a new message ID as (1) above was not adhered to. If
 * not used anywhere, proceed with the required modification.
 * NOTE: Rules for message deletion
 * 4) Check (3) and if used anywhere else, then cannot be deleted. If not used
 * anywhere, then can be deleted, but will leave a hole by design, as
 * addition rules specify modification to the end of the list and not filling
 * holes.
 */

#define GLFS_COMP_BASE_CHANGELOG GLFS_MSGID_COMP_CHANGELOG
#define GLFS_NUM_MESSAGES 54
#define GLFS_MSGID_END (GLFS_COMP_BASE_CHANGELOG + GLFS_NUM_MESSAGES + 1)

#define glfs_msg_start_x GLFS_COMP_BASE_CHANGELOG, "Invalid: Start of messages"

/*!
 * @messageid
 * @diagnosis open/opendir failed on a brick.
 * @recommended action Error number in the log should give the reason why it
 * failed. Also observe brick logs for more information.
 */
#define CHANGELOG_MSG_OPEN_FAILED               (GLFS_COMP_BASE_CHANGELOG + 1)

/*!
 * @messageid
 * @diagnosis
 * @recommended action
*/
#define CHANGELOG_MSG_NO_MEMORY                 (GLFS_COMP_BASE_CHANGELOG + 2)
/*!
 * @messageid
 * @diagnosis
 * @recommended action
*/
#define CHANGELOG_MSG_VOL_MISCONFIGURED         (GLFS_COMP_BASE_CHANGELOG + 3)

/*!
 * @messageid
 * @diagnosis
 * @recommended action
*/
#define CHANGELOG_MSG_RENAME_ERROR              (GLFS_COMP_BASE_CHANGELOG + 4)

/*!
 * @messageid
 * @diagnosis
 * @recommended action
*/
#define CHANGELOG_MSG_READ_ERROR                (GLFS_COMP_BASE_CHANGELOG + 5)

/*!
 * @messageid
 * @diagnosis
 * @recommended action
*/
#define CHANGELOG_MSG_HTIME_ERROR               (GLFS_COMP_BASE_CHANGELOG + 6)

/*!
 * @messageid
 * @diagnosis
 * @recommended action
*/
#define CHANGELOG_MSG_PTHREAD_MUTEX_INIT_FAILED (GLFS_COMP_BASE_CHANGELOG + 7)

/*!
 * @messageid
 * @diagnosis
 * @recommended action
*/
#define CHANGELOG_MSG_PTHREAD_COND_INIT_FAILED  (GLFS_COMP_BASE_CHANGELOG + 8)

/*!
 * @messageid
 * @diagnosis
 * @recommended action
*/
#define CHANGELOG_MSG_CHILD_MISCONFIGURED       (GLFS_COMP_BASE_CHANGELOG + 9)

/*!
 * @messageid
 * @diagnosis
 * @recommended action
*/
#define CHANGELOG_MSG_DIR_OPTIONS_NOT_SET       (GLFS_COMP_BASE_CHANGELOG + 10)

/*!
 * @messageid
 * @diagnosis
 * @recommended action
*/
#define CHANGELOG_MSG_CLOSE_ERROR               (GLFS_COMP_BASE_CHANGELOG + 11)

/*!
 * @messageid
 * @diagnosis
 * @recommended action
*/
#define CHANGELOG_MSG_PIPE_CREATION_ERROR       (GLFS_COMP_BASE_CHANGELOG + 12)

/*!
 * @messageid
 * @diagnosis
 * @recommended action
*/
#define CHANGELOG_MSG_DICT_GET_FAILED           (GLFS_COMP_BASE_CHANGELOG + 13)

/*!
 * @messageid
 * @diagnosis
 * @recommended action
*/
#define CHANGELOG_MSG_BARRIER_INFO              (GLFS_COMP_BASE_CHANGELOG + 14)

/*!
 * @messageid
 * @diagnosis
 * @recommended action
*/
#define CHANGELOG_MSG_BARRIER_ERROR             (GLFS_COMP_BASE_CHANGELOG + 15)

/*!
 * @messageid
 * @diagnosis
 * @recommended action
*/
#define CHANGELOG_MSG_GET_TIME_OP_FAILED        (GLFS_COMP_BASE_CHANGELOG + 16)

/*!
 * @messageid
 * @diagnosis
 * @recommended action
*/
#define CHANGELOG_MSG_WRITE_FAILED              (GLFS_COMP_BASE_CHANGELOG + 17)

/*!
 * @messageid
 * @diagnosis
 * @recommended action
*/
#define CHANGELOG_MSG_PTHREAD_ERROR             (GLFS_COMP_BASE_CHANGELOG + 18)

/*!
 * @messageid
 * @diagnosis
 * @recommended action
*/
#define CHANGELOG_MSG_INODE_NOT_FOUND           (GLFS_COMP_BASE_CHANGELOG + 19)

/*!
 * @messageid
 * @diagnosis
 * @recommended action
*/
#define CHANGELOG_MSG_FSYNC_OP_FAILED           (GLFS_COMP_BASE_CHANGELOG + 20)

/*!
 * @messageid
 * @diagnosis
 * @recommended action
*/
#define CHANGELOG_MSG_TOTAL_LOG_INFO            (GLFS_COMP_BASE_CHANGELOG + 21)

/*!
 * @messageid
 * @diagnosis
 * @recommended action
*/
#define CHANGELOG_MSG_SNAP_INFO                 (GLFS_COMP_BASE_CHANGELOG + 22)

/*!
 * @messageid
 * @diagnosis
 * @recommended action
*/
#define CHANGELOG_MSG_SELECT_FAILED             (GLFS_COMP_BASE_CHANGELOG + 23)

/*!
 * @messageid
 * @diagnosis
 * @recommended action
*/
#define CHANGELOG_MSG_FCNTL_FAILED              (GLFS_COMP_BASE_CHANGELOG + 24)

/*!
 * @messageid
 * @diagnosis
 * @recommended action
*/
#define CHANGELOG_MSG_BNOTIFY_INFO              (GLFS_COMP_BASE_CHANGELOG + 25)

/*!
 * @messageid
 * @diagnosis
 * @recommended action
*/
#define CHANGELOG_MSG_ENTRY_BUF_INFO            (GLFS_COMP_BASE_CHANGELOG + 26)

/*!
 * @messageid
 * @diagnosis
 * @recommended action
*/
#define CHANGELOG_MSG_NOT_ACTIVE                (GLFS_COMP_BASE_CHANGELOG + 27)

/*!
  @messageid
 * @diagnosis
 * @recommended action
*/
#define CHANGELOG_MSG_LOCAL_INIT_FAILED         (GLFS_COMP_BASE_CHANGELOG + 28)

/*!
  @messageid
 * @diagnosis
 * @recommended action
*/
#define CHANGELOG_MSG_NOTIFY_REGISTER_FAILED    (GLFS_COMP_BASE_CHANGELOG + 28)

/*!
  @messageid
 * @diagnosis
 * @recommended action
*/
#define CHANGELOG_MSG_PROGRAM_NAME_REG_FAILED   (GLFS_COMP_BASE_CHANGELOG + 29)

/*!
  @messageid
 * @diagnosis
 * @recommended action
*/
#define CHANGELOG_MSG_HANDLE_PROBE_ERROR        (GLFS_COMP_BASE_CHANGELOG + 30)

/*!
  @messageid
 * @diagnosis
 * @recommended action
*/
#define CHANGELOG_MSG_SET_FD_CONTEXT            (GLFS_COMP_BASE_CHANGELOG + 31)

/*!
  @messageid
 * @diagnosis
 * @recommended action
*/
#define CHANGELOG_MSG_FREEUP_FAILED             (GLFS_COMP_BASE_CHANGELOG + 32)

/*!
  @messageid
 * @diagnosis
 * @recommended action
*/
#define CHANGELOG_MSG_HTIME_INFO                (GLFS_COMP_BASE_CHANGELOG + 33)

/*!
  @messageid
 * @diagnosis
 * @recommended action
*/
#define CHANGELOG_MSG_RPC_SUBMIT_REPLY_FAILED   (GLFS_COMP_BASE_CHANGELOG + 34)

/*!
  @messageid
 * @diagnosis
 * @recommended action
*/
#define CHANGELOG_MSG_RPC_BUILD_ERROR           (GLFS_COMP_BASE_CHANGELOG + 35)

/*!
  @messageid
 * @diagnosis
 * @recommended action
*/
#define CHANGELOG_MSG_RPC_CONNECT_ERROR         (GLFS_COMP_BASE_CHANGELOG + 36)

/*!
  @messageid
 * @diagnosis
 * @recommended action
*/
#define CHANGELOG_MSG_RPC_START_ERROR           (GLFS_COMP_BASE_CHANGELOG + 37)

/*!
  @messageid
 * @diagnosis
 * @recommended action
*/
#define CHANGELOG_MSG_BUFFER_STARVATION_ERROR   (GLFS_COMP_BASE_CHANGELOG + 3)

/*!
  @messageid
 * @diagnosis
 * @recommended action
*/
#define CHANGELOG_MSG_SCAN_DIR_FAILED           (GLFS_COMP_BASE_CHANGELOG + 39)

/*!
  @messageid
 * @diagnosis
 * @recommended action
*/
#define CHANGELOG_MSG_FSETXATTR_FAILED          (GLFS_COMP_BASE_CHANGELOG + 40)

/*!
  @messageid
 * @diagnosis
 * @recommended action
*/
#define CHANGELOG_MSG_FGETXATTR_FAILED          (GLFS_COMP_BASE_CHANGELOG + 41)

/*!
  @messageid
 * @diagnosis
 * @recommended action
*/
#define CHANGELOG_MSG_CLEANUP_ON_ACTIVE_REF                             \
                                                (GLFS_COMP_BASE_CHANGELOG + 42)

/*!
  @messageid
 * @diagnosis
 * @recommended action
*/
#define CHANGELOG_MSG_DISPATCH_EVENT_FAILED      (GLFS_COMP_BASE_CHANGELOG + 43)

/*!
  @messageid
 * @diagnosis
 * @recommended action
*/
#define CHANGELOG_MSG_PUT_BUFFER_FAILED      (GLFS_COMP_BASE_CHANGELOG + 44)

/*!
 * @messageid
 * @diagnosis
 * @recommended action
*/
#define CHANGELOG_MSG_PTHREAD_COND_WAIT_FAILED  (GLFS_COMP_BASE_CHANGELOG + 45)

/*!
  @messageid
 * @diagnosis
 * @recommended action
*/
#define CHANGELOG_MSG_PTHREAD_CANCEL_FAILED      (GLFS_COMP_BASE_CHANGELOG + 46)

/*!
  @messageid
 * @diagnosis
 * @recommended action
*/
#define CHANGELOG_MSG_INJECT_FSYNC_FAILED      (GLFS_COMP_BASE_CHANGELOG + 47)

/*!
  @messageid
 * @diagnosis
 * @recommended action
*/
#define CHANGELOG_MSG_CREATE_FRAME_FAILED      (GLFS_COMP_BASE_CHANGELOG + 48)

/*!
  @messageid
 * @diagnosis
 * @recommended action
*/
#define CHANGELOG_MSG_FSTAT_OP_FAILED          (GLFS_COMP_BASE_CHANGELOG + 49)

/*!
  @messageid
 * @diagnosis
 * @recommended action
*/
#define CHANGELOG_MSG_LSEEK_OP_FAILED          (GLFS_COMP_BASE_CHANGELOG + 50)

/*!
  @messageid
 * @diagnosis
 * @recommended action
*/
#define CHANGELOG_MSG_STRSTR_OP_FAILED          (GLFS_COMP_BASE_CHANGELOG + 51)

/*!
  @messageid
 * @diagnosis
 * @recommended action
*/
#define CHANGELOG_MSG_UNLINK_OP_FAILED          (GLFS_COMP_BASE_CHANGELOG + 52)

/*!
  @messageid
 * @diagnosis
 * @recommended action
*/
#define CHANGELOG_MSG_DETECT_EMPTY_CHANGELOG_FAILED \
        (GLFS_COMP_BASE_CHANGELOG + 53)

/*!
  @messageid
 * @diagnosis
 * @recommended action
*/
#define CHANGELOG_MSG_READLINK_OP_FAILED        (GLFS_COMP_BASE_CHANGELOG + 54)

/*!
  @messageid
 * @diagnosis
 * @recommended action
*/
#define CHANGELOG_MSG_EXPLICIT_ROLLOVER_FAILED  (GLFS_COMP_BASE_CHANGELOG + 55)



#define glfs_msg_end_x GLFS_MSGID_END, "Invalid: End of messages"
#endif /* !_CHANGELOG_MESSAGES_H_ */
