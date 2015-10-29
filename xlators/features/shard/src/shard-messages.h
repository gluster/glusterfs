/*
 Copyright (c) 2015 Red Hat, Inc. <http://www.redhat.com>
 This file is part of GlusterFS.

 This file is licensed to you under your choice of the GNU Lesser
 General Public License, version 3 or any later version (LGPLv3 or
 later), or the GNU General Public License, version 2 (GPLv2), in all
 cases as published by the Free Software Foundation.
 */

#ifndef _SHARD_MESSAGES_H_
#define _SHARD_MESSAGES_H_

#include "glfs-message-id.h"

/*! \file shard-messages.h
 *  \brief shard log-message IDs and their descriptions.
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
 * 3) Check across the code if the message ID macro in question is reused
 * anywhere. If reused then the modifications should ensure correctness
 * everywhere, or needs a new message ID as (1) above was not adhered to. If
 * not used anywhere, proceed with the required modification.
 * NOTE: Rules for message deletion
 * 4) Check (3) and if used anywhere else, then cannot be deleted. If not used
 * anywhere, then can be deleted, but will leave a hole by design, as
 * addition rules specify modification to the end of the list and not filling
 * holes.
 */

#define GLFS_COMP_BASE_SHARD      GLFS_MSGID_COMP_SHARD
#define GLFS_NUM_MESSAGES         18
#define GLFS_MSGID_END          (GLFS_COMP_BASE_SHARD + GLFS_NUM_MESSAGES + 1)

#define glfs_msg_start_x GLFS_COMP_BASE_SHARD, "Invalid: Start of messages"

/*!
 * @messageid 133001
 * @diagnosis
 * @recommendedaction
 */
#define SHARD_MSG_BASE_FILE_LOOKUP_FAILED             (GLFS_COMP_BASE_SHARD + 1)


/*!
 * @messageid 133002
 * @diagnosis
 * @recommendedaction
 */
#define SHARD_MSG_DICT_SET_FAILED                     (GLFS_COMP_BASE_SHARD + 2)


/*!
 * @messageid 133003
 * @diagnosis /.shard already exists and is not a directory.
 * @recommendedaction Delete the /.shard file from the backend and try again.
 */
#define SHARD_MSG_DOT_SHARD_NODIR                     (GLFS_COMP_BASE_SHARD + 3)


/*!
 * @messageid 133004
 * @diagnosis
 * @recommendedaction
 */
#define SHARD_MSG_FD_CTX_SET_FAILED                   (GLFS_COMP_BASE_SHARD + 4)


/*!
 * @messageid 133005
 * @diagnosis
 * @recommendedaction
 */
#define SHARD_MSG_INODE_CTX_GET_FAILED                (GLFS_COMP_BASE_SHARD + 5)


/*!
 * @messageid 133006
 * @diagnosis
 * @recommendedaction
 */
#define SHARD_MSG_INODE_CTX_SET_FAILED                (GLFS_COMP_BASE_SHARD + 6)


/*!
 * @messageid 133007
 * @diagnosis
 * @recommendedaction
*/
#define SHARD_MSG_INODE_PATH_FAILED                   (GLFS_COMP_BASE_SHARD + 7)


/*!
 * @messageid 133008
 * @diagnosis
 * @recommendedaction
 */
#define SHARD_MSG_INTERNAL_XATTR_MISSING              (GLFS_COMP_BASE_SHARD + 8)


/*!
 * @messageid 133009
 * @diagnosis The client process did not get launched due to incorrect volfile.
 * @recommendedaction Possibly check to see if the volfile is correct.
 */
#define SHARD_MSG_INVALID_VOLFILE                     (GLFS_COMP_BASE_SHARD + 9)


/*!
 * @messageid 133010
 * @diagnosis
 * @recommendedaction
*/
#define SHARD_MSG_LOOKUP_SHARD_FAILED                (GLFS_COMP_BASE_SHARD + 10)

/*!
 * @messageid 133011
 * @diagnosis
 * @recommendedaction
*/
#define SHARD_MSG_MEM_ACCT_INIT_FAILED               (GLFS_COMP_BASE_SHARD + 11)

/*!
 * @messageid 133012
 * @diagnosis
 * @recommendedaction
*/
#define SHARD_MSG_NULL_THIS                          (GLFS_COMP_BASE_SHARD + 12)

/*!
 * @messageid 133013
 * @diagnosis
 * @recommendedaction
*/
#define SHARD_MSG_SIZE_SET_FAILED                    (GLFS_COMP_BASE_SHARD + 13)

/*!
 * @messageid 133014
 * @diagnosis
 * @recommendedaction
*/
#define SHARD_MSG_STAT_FAILED                        (GLFS_COMP_BASE_SHARD + 14)

/*!
 * @messageid 133015
 * @diagnosis
 * @recommendedaction
*/
#define SHARD_MSG_TRUNCATE_LAST_SHARD_FAILED         (GLFS_COMP_BASE_SHARD + 15)

/*!
 * @messageid 133016
 * @diagnosis
 * @recommendedaction
*/
#define SHARD_MSG_UPDATE_FILE_SIZE_FAILED            (GLFS_COMP_BASE_SHARD + 16)

/*!
 * @messageid 133017
 * @diagnosis The operation invoked is not supported.
 * @recommendedaction Use other syscalls to write to the file.
*/
#define SHARD_MSG_FOP_NOT_SUPPORTED                  (GLFS_COMP_BASE_SHARD + 17)

/*!
 * @messageid 133018
 * @diagnosis
 * @recommendedaction
*/
#define SHARD_MSG_INVALID_FOP                        (GLFS_COMP_BASE_SHARD + 18)

#define glfs_msg_end_x GLFS_MSGID_END, "Invalid: End of messages"
#endif /* !_SHARD_MESSAGES_H_ */
