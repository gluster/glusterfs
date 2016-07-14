/*
 Copyright (c) 2016 Red Hat, Inc. <http://www.redhat.com>
 This file is part of GlusterFS.

 This file is licensed to you under your choice of the GNU Lesser
 General Public License, version 3 or any later version (LGPLv3 or
 later), or the GNU General Public License, version 2 (GPLv2), in all
 cases as published by the Free Software Foundation.
 */

#ifndef _INDEX_MESSAGES_H_
#define _INDEX_MESSAGES_H_

#include "glfs-message-id.h"

/*! \file index-messages.h
 *  \brief INDEX log-message IDs and their descriptions.
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
 * - Retain macro naming as glfs_msg_X (for redability across developers)
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

#define GLFS_COMP_BASE_INDEX    GLFS_MSGID_COMP_INDEX
#define GLFS_NUM_MESSAGES       10
#define GLFS_MSGID_END          (GLFS_COMP_BASE_INDEX + GLFS_NUM_MESSAGES + 1)

#define glfs_msg_start_x GLFS_COMP_BASE_INDEX, "Invalid: Start of messages"

/*!
 * @messageid 138001
 * @diagnosis Index directory creation failed.
 * @recommendedaction Brick log should give the reason why it failed.
 */
#define INDEX_MSG_INDEX_DIR_CREATE_FAILED  (GLFS_COMP_BASE_INDEX + 1)

/*!
 * @messageid 138002
 * @diagnosis Index directory readdir failed.
 * @recommendedaction Brick log should give the reason why it failed.
 */
#define INDEX_MSG_INDEX_READDIR_FAILED  (GLFS_COMP_BASE_INDEX + 2)

/*!
 * @messageid 138003
 * @diagnosis Index addition failed.
 * @recommendedaction Brick log should give the reason why it failed.
 */
#define INDEX_MSG_INDEX_ADD_FAILED  (GLFS_COMP_BASE_INDEX + 3)

/*!
 * @messageid 138004
 * @diagnosis Index deletion failed.
 * @recommendedaction Brick log should give the reason why it failed.
 */
#define INDEX_MSG_INDEX_DEL_FAILED  (GLFS_COMP_BASE_INDEX + 4)

/*!
 * @messageid 138005
 * @diagnosis Setting option in dictionary failed.
 * @recommendedaction Brick log should give the reason why it failed.
 */
#define INDEX_MSG_DICT_SET_FAILED  (GLFS_COMP_BASE_INDEX + 5)

/*!
 * @messageid 138006
 * @diagnosis Setting/Getting inode data failed.
 * @recommendedaction Brick log should give the reason why it failed.
 */
#define INDEX_MSG_INODE_CTX_GET_SET_FAILED  (GLFS_COMP_BASE_INDEX + 6)

/*!
 * @messageid 138007
 * @diagnosis Invalid argments lead to the failure.
 * @recommendedaction Brick log should give more context where it failed.
 */
#define INDEX_MSG_INVALID_ARGS  (GLFS_COMP_BASE_INDEX + 7)

/*!
 * @messageid 138008
 * @diagnosis Operations on an opened file/directory failed.
 * @recommendedaction Brick log should give the reason why it failed.
 */
#define INDEX_MSG_FD_OP_FAILED  (GLFS_COMP_BASE_INDEX + 8)

/*!
 * @messageid 138009
 * @diagnosis Worker thread creation for index xlator failed.
 * @recommendedaction Brick log should give the reason why it failed.
 */
#define INDEX_MSG_WORKER_THREAD_CREATE_FAILED  (GLFS_COMP_BASE_INDEX + 9)

/*!
 * @messageid 138010
 * @diagnosis Index xlator needs to have single subvolume and at least one
 * parent subvolume, otherwise this message will come.
 * @recommendedaction Please check brick log file to find which of the above
 * two conditions failed.
 */
#define INDEX_MSG_INVALID_GRAPH  (GLFS_COMP_BASE_INDEX + 10)

#define glfs_msg_end_x GLFS_MSGID_END, "Invalid: End of messages"
#endif /* !_INDEX_MESSAGES_H_ */
