/*
  Copyright (c) 2013 Red Hat, Inc. <http://www.redhat.com>
  This file is part of GlusterFS.

  This file is licensed to you under your choice of the GNU Lesser
  General Public License, version 3 or any later version (LGPLv3 or
  later), or the GNU General Public License, version 2 (GPLv2), in all
  cases as published by the Free Software Foundation.
*/

#ifndef _QUOTA_MESSAGES_H_
#define _QUOTA_MESSAGES_H_

#ifndef _CONFIG_H
#define _CONFIG_H
#include "config.h"
#endif

#include "glfs-message-id.h"

/*! \file quota-messages.h
 *  \brief Quota log-message IDs and their descriptions
 *
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
 * 3) Check across the code if the message ID macro in question is reused
 *    anywhere. If reused then the modifications should ensure correctness
 *    everywhere, or needs a new message ID as (1) above was not adhered to. If
 *    not used anywhere, proceed with the required modification.
 * NOTE: Rules for message deletion
 * 4) Check (3) and if used anywhere else, then cannot be deleted. If not used
 *    anywhere, then can be deleted, but will leave a hole by design, as
 *    addition rules specify modification to the end of the list and not filling
 *    holes.
 */

#define GLFS_QUOTA_BASE          GLFS_MSGID_COMP_QUOTA
#define GLFS_NUM_MESSAGES        23
#define GLFS_MSGID_END     (GLFS_QUOTA_BASE + GLFS_NUM_MESSAGES + 1)
/* Messaged with message IDs */
#define glfs_msg_start_x GLFS_QUOTA_BASE, "Invalid: Start of messages"
/*------------*/

/*!
 * @messageid 120001
 * @diagnosis Quota enforcement has failed.
 * @recommendedaction None
 */
#define Q_MSG_ENFORCEMENT_FAILED        (GLFS_QUOTA_BASE + 1)


/*!
* @messageid 120002
* @diagnosis system is out of memory
* @recommendedaction None
*/
#define Q_MSG_ENOMEM    (GLFS_QUOTA_BASE + 2)

/*!
 * @messageid 120003
 * @diagnosis Parent inode is not present in the  inode table due to the
 * inode table limits or the brick was restarted recently.
 * @recommendedaction If it is a brick restart then perform a crawl on the
 * file system or the specific directory in which the problem is observed.
 * If inode table limit has been reached,please increase the limit of
 * network.inode-lru-limit to a higher value(can be set through CLI).
 */
#define Q_MSG_PARENT_NULL       (GLFS_QUOTA_BASE + 3)

/*!
 * @messageid 120004
 * @diagnosis This is to inform the admin that the user has crossed the soft limit
 * of the quota configured on the directory and expected to cross the hard limit soon.
 * @recommendedaction You may reconfigure your quota limits.
 */
#define Q_MSG_CROSSED_SOFT_LIMIT        (GLFS_QUOTA_BASE + 4)

/*!
 * @messageid 120005
 * @diagnosis Quota translator failed to connect to quotad. This could be
 * due to one or more of the following reasons, (1) Quotad is not running.
 * (2) Brick process has run out of memory.
 * @recommendedaction If quotad is not running, consider starting quotad.
 * else check system memory consumption.
 */
#define Q_MSG_QUOTA_ENFORCER_RPC_INIT_FAILED            (GLFS_QUOTA_BASE + 5)

/*!
 * @messageid 120006
 * @diagnosis Getting cluster-wide size failed
 * @recommendedaction Restart quotad. Kill quotad by searching
 * "ps ax | grep quotad" and use volume start force to restart it.
 */

#define Q_MSG_REMOTE_OPERATION_FAILED   (GLFS_QUOTA_BASE + 6)

/*!
 * @messageid 120007
 * @diagnosis Updation of global quota size failed. This may be due to quotad
 * is down or lost connection with quotad.
 * @recommendedaction Please restart quotad.
 */

#define Q_MSG_FAILED_TO_SEND_FOP        (GLFS_QUOTA_BASE + 7)

/*!
 * @messageid 120008
 * @diagnosis
 * @recommendedaction Check volfile for correctness
 */

#define Q_MSG_INVALID_VOLFILE        (GLFS_QUOTA_BASE + 8)

/*!
 * @messageid 120009
 * @diagnosis
 * @recommendedaction
 */

#define Q_MSG_INODE_PARENT_NOT_FOUND        (GLFS_QUOTA_BASE + 9)

/*!
 * @messageid 120010
 * @diagnosis
 * @recommendedaction
 */

#define Q_MSG_XDR_DECODE_ERROR        (GLFS_QUOTA_BASE + 10)

/*!
 * @messageid 120011
 * @diagnosis
 * @recommendedaction
 */

#define Q_MSG_DICT_UNSERIALIZE_FAIL        (GLFS_QUOTA_BASE + 11)

/*!
 * @messageid 120012
 * @diagnosis
 * @recommendedaction
 */

#define Q_MSG_DICT_SERIALIZE_FAIL        (GLFS_QUOTA_BASE + 12)

/*!
 * @messageid 120013
 * @diagnosis
 * @recommendedaction
 */

#define Q_MSG_RPCSVC_INIT_FAILED        (GLFS_QUOTA_BASE + 13)

/*!
 * @messageid 120014
 * @diagnosis
 * @recommendedaction
 */

#define Q_MSG_RPCSVC_LISTENER_CREATION_FAILED        (GLFS_QUOTA_BASE + 14)

/*!
 * @messageid 120015
 * @diagnosis
 * @recommendedaction
 */

#define Q_MSG_RPCSVC_REGISTER_FAILED        (GLFS_QUOTA_BASE + 15)

/*!
 * @messageid 120016
 * @diagnosis
 * @recommendedaction
 */

#define Q_MSG_XDR_DECODING_FAILED        (GLFS_QUOTA_BASE + 16)
/*!
 * @messageid 120017
 * @diagnosis
 * @recommendedaction
 */

#define Q_MSG_RPCCLNT_REGISTER_NOTIFY_FAILED        (GLFS_QUOTA_BASE + 17)
/*!
 * @messageid 120018
 * @diagnosis
 * @recommendedaction Umount and mount the corresponing volume
 */

#define Q_MSG_ANCESTRY_BUILD_FAILED        (GLFS_QUOTA_BASE + 18)

/*!
 * @messageid 120019
 * @diagnosis
 * @recommendedaction
 */

#define Q_MSG_SIZE_KEY_MISSING        (GLFS_QUOTA_BASE + 19)

/*!
 * @messageid 120020
 * @diagnosis
 * @recommendedaction
 */

#define Q_MSG_INODE_CTX_GET_FAILED     (GLFS_QUOTA_BASE + 20)

/*!
 * @messageid 120021
 * @diagnosis
 * @recommendedaction
 */

#define Q_MSG_INODE_CTX_SET_FAILED     (GLFS_QUOTA_BASE + 21)

/*!
 * @messageid 120022
 * @diagnosis
 * @recommendedaction
 */

#define Q_MSG_LOOKUP_FAILED     (GLFS_QUOTA_BASE + 22)

/*!
 * @messageid 120023
 * @diagnosis
 * @recommendedaction
 */

#define Q_MSG_RPC_SUBMIT_FAILED     (GLFS_QUOTA_BASE + 23)

/*------------*/
#define glfs_msg_end_x GLFS_MSGID_END, "Invalid: End of messages"

#endif /* !_QUOTA_MESSAGES_H_ */

