/*
  Copyright (c) 2014 Red Hat, Inc. <http://www.redhat.com>
  This file is part of GlusterFS.

  This file is licensed to you under your choice of the GNU Lesser
  General Public License, version 3 or any later version (LGPLv3 or
  later), or the GNU General Public License, version 2 (GPLv2), in all
  cases as published by the Free Software Foundation.
*/

#ifndef _GLUSTERD_MESSAGES_H_
#define _GLUSTERD_MESSAGES_H_

#ifndef _CONFIG_H
#define _CONFIG_H
#include "config.h"
#endif

#include "glfs-message-id.h"

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

#define GLUSTERD_COMP_BASE      GLFS_MSGID_GLUSTERD
#define GLFS_NUM_MESSAGES       18
#define GLFS_MSGID_END          (GLUSTERD_COMP_BASE + GLFS_NUM_MESSAGES + 1)
/* Messaged with message IDs */
#define glfs_msg_start_x GLFS_COMP_BASE, "Invalid: Start of messages"
/*------------*/

#define GD_MSG_SERVER_QUORUM_NOT_MET (GLUSTERD_COMP_BASE + 1)

#define GD_MSG_SERVER_QUORUM_LOST_STOPPING_BRICKS (GLUSTERD_COMP_BASE + 2)

#define GD_MSG_SERVER_QUORUM_MET_STARTING_BRICKS (GLUSTERD_COMP_BASE + 3)

#define GD_MSG_PEER_DISCONNECTED (GLUSTERD_COMP_BASE + 4)

#define GD_MSG_BRICK_DISCONNECTED (GLUSTERD_COMP_BASE + 5)

#define GD_MSG_NODE_DISCONNECTED (GLUSTERD_COMP_BASE + 6)

#define GD_MSG_REBALANCE_DISCONNECTED (GLUSTERD_COMP_BASE + 7)

/* 4 to 7 is reserved for brick disconnection messages */

#define GD_MSG_VOL_CLEANUP_FAIL (GLUSTERD_COMP_BASE + 8)

#define GD_MSG_VOL_VERS_MISMATCH (GLUSTERD_COMP_BASE + 9)

#define GD_MSG_CKSUM_VERS_MISMATCH (GLUSTERD_COMP_BASE + 10)

#define GD_MSG_QUOTA_CONFIG_VERS_MISMATCH (GLUSTERD_COMP_BASE + 11)

#define GD_MSG_QUOTA_CONFIG_CKSUM_MISMATCH (GLUSTERD_COMP_BASE + 12)

#define GD_MSG_BRICK_STOP_FAIL (GLUSTERD_COMP_BASE + 13)

#define GD_MSG_SVC_KILL_FAIL (GLUSTERD_COMP_BASE + 14)

#define GD_MSG_PID_KILL_FAIL (GLUSTERD_COMP_BASE + 15)

#define GD_MSG_REBAL_NO_SOCK_FILE (GLUSTERD_COMP_BASE + 16)

#define GD_MSG_UNIX_OP_BUILD_FAIL (GLUSTERD_COMP_BASE + 17)

#define GD_MSG_RPC_CREATE_FAIL (GLUSTERD_COMP_BASE + 18)
/*------------*/
#define glfs_msg_end_x GLFS_MSGID_END, "Invalid: End of messages"

#endif /* !_GLUSTERD_MESSAGES_H_ */
