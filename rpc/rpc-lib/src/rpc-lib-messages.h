/*
  Copyright (c) 2015 Red Hat, Inc. <http://www.redhat.com>
  This file is part of GlusterFS.

  This file is licensed to you under your choice of the GNU Lesser
  General Public License, version 3 or any later version (LGPLv3 or
  later), or the GNU General Public License, version 2 (GPLv2), in all
  cases as published by the Free Software Foundation.
*/

#ifndef _RPC_LIB_MESSAGES_H_
#define _RPC_LIB_MESSAGES_H_

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

#define GLFS_RPC_LIB_BASE        GLFS_MSGID_COMP_RPC_LIB
#define GLFS_NUM_MESSAGES        13
#define GLFS_RPC_LIB_MSGID_END   (GLFS_RPC_LIB_BASE + GLFS_NUM_MESSAGES + 1)

/* Messages with message IDs */

#define glfs_msg_start_x GLFS_RPC_LIB_BASE, "Invalid: Start of messages"

/*------------*/
/* First slot is allocated for common transport msg ids */

#define TRANS_MSG_ADDR_FAMILY_NOT_SPECIFIED          (GLFS_RPC_LIB_BASE + 1)

#define TRANS_MSG_UNKNOWN_ADDR_FAMILY                (GLFS_RPC_LIB_BASE + 2)

#define TRANS_MSG_REMOTE_HOST_ERROR                  (GLFS_RPC_LIB_BASE + 3)

#define TRANS_MSG_DNS_RESOL_FAILED                   (GLFS_RPC_LIB_BASE + 4)

#define TRANS_MSG_LISTEN_PATH_ERROR                  (GLFS_RPC_LIB_BASE + 5)

#define TRANS_MSG_CONNECT_PATH_ERROR                 (GLFS_RPC_LIB_BASE + 6)

#define TRANS_MSG_GET_ADDR_INFO_FAILED               (GLFS_RPC_LIB_BASE + 7)

#define TRANS_MSG_PORT_BIND_FAILED                   (GLFS_RPC_LIB_BASE + 8)

#define TRANS_MSG_INET_ERROR                         (GLFS_RPC_LIB_BASE + 9)

#define TRANS_MSG_GET_NAME_INFO_FAILED               (GLFS_RPC_LIB_BASE + 10)

#define TRANS_MSG_TRANSPORT_ERROR                    (GLFS_RPC_LIB_BASE + 11)

#define TRANS_MSG_TIMEOUT_EXCEEDED                   (GLFS_RPC_LIB_BASE + 12)

#define TRANS_MSG_SOCKET_BIND_ERROR                  (GLFS_RPC_LIB_BASE + 13)

/*------------*/

#define glfs_msg_end_x GLFS_RPC_LIB_MSGID_END, "Invalid: End of messages"

#endif /* !_RPC_LIB_MESSAGES_H_ */

