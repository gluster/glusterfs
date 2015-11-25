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

/* file bit-rot-stub-messages.h
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

#define GLFS_BITROT_STUB_BASE                   GLFS_MSGID_COMP_BITROT_STUB
#define GLFS_BITROT_STUB_NUM_MESSAGES           31
#define GLFS_MSGID_END         (GLFS_BITROT_STUB_BASE + \
                                GLFS_BITROT_STUB_NUM_MESSAGES + 1)
/* Messaged with message IDs */
#define glfs_msg_start_x   GLFS_BITROT_STUB_BASE, "Invalid: Start of messages"
/*------------*/


#define BRS_MSG_NO_MEMORY                   (GLFS_BITROT_STUB_BASE + 1)
/*!
 * @messageid
 * @diagnosis
 * @recommendedaction
 *
 */
#define BRS_MSG_SET_EVENT_FAILED            (GLFS_BITROT_STUB_BASE + 2)
/*!
 * @messageid
 * @diagnosis
 * @recommendedaction
 *
 */
#define BRS_MSG_MEM_ACNT_FAILED             (GLFS_BITROT_STUB_BASE + 3)
/*!
 * @messageid
 * @diagnosis
 * @recommendedaction
 *
 */
#define BRS_MSG_CREATE_FRAME_FAILED         (GLFS_BITROT_STUB_BASE + 4)
/*!
 * @messageid
 * @diagnosis
 * @recommendedaction
 *
 */
#define BRS_MSG_SET_CONTEXT_FAILED          (GLFS_BITROT_STUB_BASE + 5)
/*!
 * @messageid
 * @diagnosis
 * @recommendedaction
 *
 */
#define BRS_MSG_CHANGE_VERSION_FAILED       (GLFS_BITROT_STUB_BASE + 6)
/*!
 * @messageid
 * @diagnosis
 * @recommendedaction
 *
 */
#define BRS_MSG_ADD_FD_TO_LIST_FAILED       (GLFS_BITROT_STUB_BASE + 7)
/*!
 * @messageid
 * @diagnosis
 * @recommendedaction
 *
 */
#define BRS_MSG_SET_FD_CONTEXT_FAILED       (GLFS_BITROT_STUB_BASE + 8)
/*!
 * @messageid
 * @diagnosis
 * @recommendedaction
 *
 */
#define BRS_MSG_CREATE_ANONYMOUS_FD_FAILED  (GLFS_BITROT_STUB_BASE + 9)
/*!
 * @messageid
 * @diagnosis
 * @recommendedaction
 *
 */
#define BRS_MSG_NO_CHILD                    (GLFS_BITROT_STUB_BASE + 10)
/*!
 * @messageid
 * @diagnosis
 * @recommendedaction
 *
 */
#define BRS_MSG_STUB_ALLOC_FAILED           (GLFS_BITROT_STUB_BASE + 11)
/*!
 * @messageid
 * @diagnosis
 * @recommendedaction
 *
 */
#define BRS_MSG_GET_INODE_CONTEXT_FAILED    (GLFS_BITROT_STUB_BASE + 12)
/*!
 * @messageid
 * @diagnosis
 * @recommendedaction
 *
 */
#define BRS_MSG_CANCEL_SIGN_THREAD_FAILED   (GLFS_BITROT_STUB_BASE + 13)
/*!
 * @messageid
 * @diagnosis
 * @recommendedaction
 *
 */
#define BRS_MSG_ADD_FD_TO_INODE             (GLFS_BITROT_STUB_BASE + 14)
/*!
 * @messageid
 * @diagnosis
 * @recommendedaction
 *
 */
#define BRS_MSG_SIGN_VERSION_ERROR          (GLFS_BITROT_STUB_BASE + 15)
/*!
 * @messageid
 * @diagnosis
 * @recommendedaction
 *
 */
#define BRS_MSG_BAD_OBJ_MARK_FAIL           (GLFS_BITROT_STUB_BASE + 16)
/*!
 * @messageid
 * @diagnosis
 * @recommendedaction
 *
 */
#define BRS_MSG_NON_SCRUB_BAD_OBJ_MARK      (GLFS_BITROT_STUB_BASE + 17)
/*!
 * @messageid
 * @diagnosis
 * @recommendedaction
 *
 */
#define BRS_MSG_REMOVE_INTERNAL_XATTR       (GLFS_BITROT_STUB_BASE + 18)
/*!
 * @messageid
 * @diagnosis
 * @recommendedaction
 *
 */
#define BRS_MSG_SET_INTERNAL_XATTR          (GLFS_BITROT_STUB_BASE + 19)
/*!
 * @messageid
 * @diagnosis
 * @recommendedaction
 *
 */
#define BRS_MSG_BAD_OBJECT_ACCESS           (GLFS_BITROT_STUB_BASE + 20)
/*!
 * @messageid
 * @diagnosis
 * @recommendedaction
 *
 */
#define BRS_MSG_BAD_CONTAINER_FAIL          (GLFS_BITROT_STUB_BASE + 21)
/*!
 * @messageid
 * @diagnosis
 * @recommendedaction
 *
 */
#define BRS_MSG_BAD_OBJECT_DIR_FAIL        (GLFS_BITROT_STUB_BASE + 22)
/*!
 * @messageid
 * @diagnosis
 * @recommendedaction
 *
 */
#define BRS_MSG_BAD_OBJECT_DIR_SEEK_FAIL   (GLFS_BITROT_STUB_BASE + 23)
/*!
 * @messageid
 * @diagnosis
 * @recommendedaction
 *
 */
#define BRS_MSG_BAD_OBJECT_DIR_TELL_FAIL   (GLFS_BITROT_STUB_BASE + 24)
/*!
 * @messageid
 * @diagnosis
 * @recommendedaction
 *
 */
#define BRS_MSG_BAD_OBJECT_DIR_READ_FAIL   (GLFS_BITROT_STUB_BASE + 25)
/*!
 * @messageid
 * @diagnosis
 * @recommendedaction
 *
 */
#define BRS_MSG_GET_FD_CONTEXT_FAILED      (GLFS_BITROT_STUB_BASE + 26)
/*!
 * @messageid
 * @diagnosis
 * @recommendedaction
 *
 */
#define BRS_MSG_BAD_HANDLE_DIR_NULL        (GLFS_BITROT_STUB_BASE + 27)
/*!
 * @messageid
 * @diagnosis
 * @recommendedaction
 *
 */
#define BRS_MSG_BAD_OBJ_THREAD_FAIL        (GLFS_BITROT_STUB_BASE + 28)
/*!
 * @messageid
 * @diagnosis
 * @recommendedaction
 *
 */
#define BRS_MSG_BAD_OBJ_DIR_CLOSE_FAIL     (GLFS_BITROT_STUB_BASE + 29)
/*!
 * @messageid
 * @diagnosis
 * @recommendedaction
 *
 */
#define BRS_MSG_LINK_FAIL                  (GLFS_BITROT_STUB_BASE + 30)
/*!
 * @messageid
 * @diagnosis
 * @recommendedaction
 *
 */
#define BRS_MSG_BAD_OBJ_UNLINK_FAIL        (GLFS_BITROT_STUB_BASE + 31)
/*!
 * @messageid
 * @diagnosis
 * @recommendedaction
 *
 */
/*------------*/

#define glfs_msg_end_x GLFS_MSGID_END, "Invalid: End of messages"
#endif /* !_BITROT_STUB_MESSAGES_H_ */
