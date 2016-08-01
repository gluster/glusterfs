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

#ifndef _CONFIG_H
#define _CONFIG_H
#include "config.h"
#endif

#include "glfs-message-id.h"

/*! \file changelog-lib-messages.h
 *  \brief CHANGELOG_LIB log-message IDs and their descriptions.
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

#define GLFS_COMP_BASE_CHANGELOG_LIB GLFS_MSGID_COMP_CHANGELOG_LIB
#define GLFS_NUM_MESSAGES 32
#define GLFS_MSGID_END (GLFS_COMP_BASE_CHANGELOG_LIB + GLFS_NUM_MESSAGES + 1)

#define glfs_msg_start_x GLFS_COMP_BASE_CHANGELOG_LIB,\
        "Invalid: Start of messages"

/*!
 * @messageid
 * @diagnosis open/opendir failed on a brick.
 * @recommended action Error number in the log should give the reason why it
 * failed. Also observe brick logs for more information.
 */
#define CHANGELOG_LIB_MSG_OPEN_FAILED        (GLFS_COMP_BASE_CHANGELOG_LIB + 1)

/*!
 * @messageid
 * @diagnosis
 * @recommended action
*/
#define CHANGELOG_LIB_MSG_FAILED_TO_RMDIR    (GLFS_COMP_BASE_CHANGELOG_LIB + 2)

/*!
 * @messageid
 * @diagnosis
 * @recommended action
*/
#define CHANGELOG_LIB_MSG_SCRATCH_DIR_ENTRIES_CREATION_ERROR                \
(GLFS_COMP_BASE_CHANGELOG_LIB + 3)

/*!
 * @messageid
 * @diagnosis
 * @recommended action
*/
#define CHANGELOG_LIB_MSG_THREAD_CREATION_FAILED   \
        (GLFS_COMP_BASE_CHANGELOG_LIB + 4)

/*!
 * @messageid
 * @diagnosis
 * @recommended action
*/
#define CHANGELOG_LIB_MSG_OPENDIR_ERROR    (GLFS_COMP_BASE_CHANGELOG_LIB + 5)

/*!
 * @messageid
 * @diagnosis
 * @recommended action
*/
#define CHANGELOG_LIB_MSG_RENAME_FAILED     (GLFS_COMP_BASE_CHANGELOG_LIB + 6)

/*!
 * @messageid
 * @diagnosis
 * @recommended action
*/
#define CHANGELOG_LIB_MSG_READ_ERROR       (GLFS_COMP_BASE_CHANGELOG_LIB + 7)

/*!
 * @messageid
 * @diagnosis
 * @recommended action
*/
#define CHANGELOG_LIB_MSG_HTIME_ERROR      (GLFS_COMP_BASE_CHANGELOG_LIB + 8)

/*!
 * @messageid
 * @diagnosis
 * @recommended action
*/
#define CHANGELOG_LIB_MSG_GET_TIME_ERROR   (GLFS_COMP_BASE_CHANGELOG_LIB + 9)

/*!
 * @messageid
 * @diagnosis
 * @recommended action
*/
#define CHANGELOG_LIB_MSG_WRITE_FAILED    (GLFS_COMP_BASE_CHANGELOG_LIB + 10)

/*!
 * @messageid
 * @diagnosis
 * @recommended action
*/
#define CHANGELOG_LIB_MSG_PTHREAD_ERROR    (GLFS_COMP_BASE_CHANGELOG_LIB + 11)

/*!
 * @messageid
 * @diagnosis
 * @recommended action
*/
#define CHANGELOG_LIB_MSG_MMAP_FAILED      (GLFS_COMP_BASE_CHANGELOG_LIB + 12)

/*!
 * @messageid
 * @diagnosis
 * @recommended action
*/
#define CHANGELOG_LIB_MSG_MUNMAP_FAILED    (GLFS_COMP_BASE_CHANGELOG_LIB + 13)

/*!
 * @messageid
 * @diagnosis
 * @recommended action
*/
#define CHANGELOG_LIB_MSG_ASCII_ERROR     (GLFS_COMP_BASE_CHANGELOG_LIB + 14)

/*!
 * @messageid
 * @diagnosis
 * @recommended action
*/
#define CHANGELOG_LIB_MSG_STAT_FAILED     (GLFS_COMP_BASE_CHANGELOG_LIB + 15)

/*!
 * @messageid
 * @diagnosis
 * @recommended action
*/
#define CHANGELOG_LIB_MSG_GET_XATTR_FAILED \
        (GLFS_COMP_BASE_CHANGELOG_LIB + 16)

/*!
 * @messageid
 * @diagnosis
 * @recommended action
*/
#define CHANGELOG_LIB_MSG_PUBLISH_ERROR   (GLFS_COMP_BASE_CHANGELOG_LIB + 17)

/*!
 * @messageid
 * @diagnosis
 * @recommended action
*/
#define CHANGELOG_LIB_MSG_PARSE_ERROR     (GLFS_COMP_BASE_CHANGELOG_LIB + 18)

/*!
 * @messageid
 * @diagnosis
 * @recommended action
*/
#define CHANGELOG_LIB_MSG_TOTAL_LOG_INFO  (GLFS_COMP_BASE_CHANGELOG_LIB + 19)

/*!
 * @messageid
 * @diagnosis
 * @recommended action
*/
#define CHANGELOG_LIB_MSG_CLEANUP_ERROR   (GLFS_COMP_BASE_CHANGELOG_LIB + 20)

/*!
 * @messageid
 * @diagnosis
 * @recommended action
*/
#define CHANGELOG_LIB_MSG_UNLINK_FAILED   (GLFS_COMP_BASE_CHANGELOG_LIB + 21)

/*!
  @messageid
 * @diagnosis
 * @recommended action
*/
#define CHANGELOG_LIB_MSG_NOTIFY_REGISTER_FAILED\
        (GLFS_COMP_BASE_CHANGELOG_LIB + 22)

/*!
  @messageid
 * @diagnosis
 * @recommended action
*/
#define CHANGELOG_LIB_MSG_INVOKE_RPC_FAILED\
        (GLFS_COMP_BASE_CHANGELOG_LIB + 23)

/*!
  @messageid
 * @diagnosis
 * @recommended action
*/
#define CHANGELOG_LIB_MSG_DRAINING_EVENT_INFO\
        (GLFS_COMP_BASE_CHANGELOG_LIB + 24)

/*!
  @messageid
 * @diagnosis
 * @recommended action
*/
#define CHANGELOG_LIB_MSG_CLEANING_BRICK_ENTRY_INFO \
        (GLFS_COMP_BASE_CHANGELOG_LIB + 25)

/*!
  @messageid
 * @diagnosis
 * @recommended action
*/
#define CHANGELOG_LIB_MSG_FREEING_ENTRY_INFO \
        (GLFS_COMP_BASE_CHANGELOG_LIB + 26)

/*!
  @messageid
 * @diagnosis
 * @recommended action
*/
#define CHANGELOG_LIB_MSG_XDR_DECODING_FAILED \
        (GLFS_COMP_BASE_CHANGELOG_LIB + 27)

/*!
  @messageid
 * @diagnosis
 * @recommended action
*/
#define CHANGELOG_LIB_MSG_NOTIFY_REGISTER_INFO \
        (GLFS_COMP_BASE_CHANGELOG_LIB + 28)

/*!
  @messageid
 * @diagnosis
 * @recommended action
*/
#define CHANGELOG_LIB_MSG_THREAD_CLEANUP_WARNING \
        (GLFS_COMP_BASE_CHANGELOG_LIB + 29)

/*!
  @messageid
 * @diagnosis
 * @recommended action
*/
#define CHANGELOG_LIB_MSG_COPY_FROM_BUFFER_FAILED \
        (GLFS_COMP_BASE_CHANGELOG_LIB + 30)

/*!
 * @messageid
 * @diagnosis
 * @recommended action
*/
#define CHANGELOG_LIB_MSG_PTHREAD_JOIN_FAILED \
        (GLFS_COMP_BASE_CHANGELOG_LIB + 31)

/*!
 * @messageid
 * @diagnosis
 * @recommended action
*/
#define CHANGELOG_LIB_MSG_HIST_FAILED \
        (GLFS_COMP_BASE_CHANGELOG_LIB + 32)

#define glfs_msg_end_x GLFS_MSGID_END, "Invalid: End of messages"
#endif /* !_CHANGELOG_MESSAGES_H_ */
