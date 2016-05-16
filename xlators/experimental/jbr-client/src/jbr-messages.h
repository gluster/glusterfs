/*
  Copyright (c) 2015 Red Hat, Inc. <http://www.redhat.com>
  This file is part of GlusterFS.

  This file is licensed to you under your choice of the GNU Lesser
  General Public License, version 3 or any later version (LGPLv3 or
  later), or the GNU General Public License, version 2 (GPLv2), in all
  cases as published by the Free Software Foundation.
*/

#ifndef _JBR_MESSAGES_H_
#define _JBR_MESSAGES_H_

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

#define JBR_COMP_BASE           GLFS_MSGID_COMP_JBR
#define GLFS_NUM_MESSAGES       1
#define GLFS_MSGID_END          (JBR_COMP_BASE + GLFS_NUM_MESSAGES + 1)

/*!
 * @messageid
 * @diagnosis
 * @recommendedaction
 */
#define J_MSG_INIT_FAIL                      (JBR_COMP_BASE + 1)

/*!
 * @messageid
 * @diagnosis
 * @recommendedaction
 */
#define J_MSG_RETRY_MSG                      (JBR_COMP_BASE + 2)

/*!
 * @messageid
 * @diagnosis
 * @recommendedaction
 */
#define J_MSG_MEM_ERR                        (JBR_COMP_BASE + 3)

/*!
 * @messageid
 * @diagnosis
 * @recommendedaction
 */
#define J_MSG_DICT_FLR                       (JBR_COMP_BASE + 4)

/*!
 * @messageid
 * @diagnosis
 * @recommendedaction
 */
#define J_MSG_GENERIC                        (JBR_COMP_BASE + 5)

/*!
 * @messageid
 * @diagnosis
 * @recommendedaction
 */
#define J_MSG_INVALID                        (JBR_COMP_BASE + 6)

/*!
 * @messageid
 * @diagnosis
 * @recommendedaction
 */
#define J_MSG_NO_DATA                        (JBR_COMP_BASE + 7)

/*!
 * @messageid
 * @diagnosis
 * @recommendedaction
 */
#define J_MSG_SYS_CALL_FAILURE               (JBR_COMP_BASE + 8)

/*!
 * @messageid
 * @diagnosis
 * @recommendedaction
 */
#define J_MSG_QUORUM_NOT_MET                 (JBR_COMP_BASE + 9)

/*!
 * @messageid
 * @diagnosis
 * @recommendedaction
 */
#define J_MSG_LOCK_FAILURE                  (JBR_COMP_BASE + 10)


#endif /* _JBR_MESSAGES_H_ */
