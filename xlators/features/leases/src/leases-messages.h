/*
 Copyright (c) 2015-2016 Red Hat, Inc. <http://www.redhat.com>
 This file is part of GlusterFS.

 This file is licensed to you under your choice of the GNU Lesser
 General Public License, version 3 or any later version (LGPLv3 or
 later), or the GNU General Public License, version 2 (GPLv2), in all
 cases as published by the Free Software Foundation.
 */

#ifndef _LEASES_MESSAGES_H_
#define _LEASES_MESSAGES_H_

#include "glfs-message-id.h"

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
 * anywhere. If reused then then the modifications should ensure correctness
 * everywhere, or needs a new message ID as (1) above was not adhered to. If
 * not used anywhere, proceed with the required modification.
 * NOTE: Rules for message deletion
 * 4) Check (3) and if used anywhere else, then cannot be deleted. If not used
 * anywhere, then can be deleted, but will leave a hole by design, as
 * addition rules specify modification to the end of the list and not filling
 * holes.
 */

#define LEASES_COMP_BASE        GLFS_MSGID_COMP_LEASES
#define GLFS_NUM_MESSAGES       11
#define GLFS_MSGID_END          (LEASES_COMP_BASE + GLFS_NUM_MESSAGES + 1)

#define glfs_msg_start_x LEASES_COMP_BASE, "Invalid: Start of messages"
/*------------*/

/*!
 * @messageid
 * @diagnosis
 * @recommendedaction
 */
#define LEASE_MSG_NO_MEM                        (LEASES_COMP_BASE + 1)

/*!
 * @messageid
 * @diagnosis
 * @recommendedaction
 */
#define LEASE_MSG_RECALL_FAIL                   (LEASES_COMP_BASE + 2)

/*!
 * @messageid
 * @diagnosis
 * @recommendedaction
 */
#define LEASE_MSG_INVAL_LEASE_ID                (LEASES_COMP_BASE + 3)

/*!
 * @messageid
 * @diagnosis
 * @recommendedaction
 */
#define LEASE_MSG_INVAL_UNLK_LEASE              (LEASES_COMP_BASE + 4)

/*!
 * @messageid
 * @diagnosis
 * @recommendedaction
 */
#define LEASE_MSG_INVAL_INODE_CTX               (LEASES_COMP_BASE + 5)

/*!
 * @messageid
 * @diagnosis
 * @recommendedaction
 */
#define LEASE_MSG_NOT_ENABLED                   (LEASES_COMP_BASE + 6)

/*!
 * @messageid
 * @diagnosis
 * @recommendedaction
 */
#define LEASE_MSG_NO_TIMER_WHEEL                (LEASES_COMP_BASE + 7)

/*!
 * @messageid
 * @diagnosis
 * @recommendedaction
 */
#define LEASE_MSG_CLNT_NOTFOUND                 (LEASES_COMP_BASE + 8)

/*!
 * @messageid
 * @diagnosis
 * @recommendedaction
 */
#define LEASE_MSG_INODE_NOTFOUND                (LEASES_COMP_BASE + 9)

/*!
 * @messageid
 * @diagnosis
 * @recommendedaction
 */
#define LEASE_MSG_INVAL_FD_CTX                  (LEASES_COMP_BASE + 10)

/*!
 * @messageid
 * @diagnosis
 * @recommendedaction
 */
#define LEASE_MSG_INVAL_LEASE_TYPE              (LEASES_COMP_BASE + 11)

/*!
 * @messageid
 * @diagnosis
 * @recommendedaction
 */
#define glfs_msg_end_x GLFS_MSGID_END, "Invalid: End of messages"

#endif /* !_LEASES_MESSAGES_H_ */
