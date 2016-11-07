/*
 Copyright (c) 2015 Red Hat, Inc. <http://www.redhat.com>
 This file is part of GlusterFS.

 This file is licensed to you under your choice of the GNU Lesser
 General Public License, version 3 or any later version (LGPLv3 or
 later), or the GNU General Public License, version 2 (GPLv2), in all
 cases as published by the Free Software Foundation.
 */

#ifndef _AFR_MESSAGES_H_
#define _AFR_MESSAGES_H_

#include "glfs-message-id.h"

/*! \file afr-messages.h
 *  \brief AFR log-message IDs and their descriptions.
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

#define GLFS_COMP_BASE_AFR      GLFS_MSGID_COMP_AFR
#define GLFS_NUM_MESSAGES       42
#define GLFS_MSGID_END          (GLFS_COMP_BASE_AFR + GLFS_NUM_MESSAGES + 1)

#define glfs_msg_start_x GLFS_COMP_BASE_AFR, "Invalid: Start of messages"

/*!
 * @messageid 108001
 * @diagnosis Client quorum is not met due to which file modification
 * operations are disallowed.
 * @recommendedaction Some brick processes are down/ not visible from the
 * client. Ensure that the bricks are up/ network traffic is not blocked.
 */
#define AFR_MSG_QUORUM_FAIL             (GLFS_COMP_BASE_AFR + 1)


/*!
 * @messageid 108002
 * @diagnosis The bricks that were down are now up and quorum is restored.
 * @recommendedaction Possibly check why the bricks went down to begin with.
 */
#define AFR_MSG_QUORUM_MET              (GLFS_COMP_BASE_AFR + 2)


/*!
 * @messageid 108003
 * @diagnosis Client quorum-type was set to auto due to which the quorum-count
 * option is no longer valid.
 * @recommendedaction None.
 */
#define AFR_MSG_QUORUM_OVERRIDE         (GLFS_COMP_BASE_AFR + 3)


/*!
 * @messageid 108004
 * @diagnosis Replication sub volume witnessed a connection notification
 * from a brick which does not belong to its replica set.
 * @recommendedaction None. This is a safety check in code.
 */
#define AFR_MSG_INVALID_CHILD_UP        (GLFS_COMP_BASE_AFR + 4)


/*!
 * @messageid 108005
 * @diagnosis A replica set that was inaccessible because all its bricks were
 * down is now accessible because at least one of its bricks came back up.
 * @recommendedaction Possibly check why all the bricks of that replica set
 * went down to begin with.
 */
#define AFR_MSG_SUBVOL_UP               (GLFS_COMP_BASE_AFR + 5)


/*!
 * @messageid 108006
 * @diagnosis bricks of a replica set are down. Data residing in that
 * replica cannot be accessed until one of the bricks come back up.
 * @recommendedaction Ensure that the bricks are up.
 */
#define AFR_MSG_SUBVOLS_DOWN            (GLFS_COMP_BASE_AFR + 6)


/*!
 * @messageid 108007
 * @diagnosis Entry unlocks failed on a brick.
 * @recommendedaction Error number in the log should give the reason why it
 * failed. Also observe brick logs for more information.
*/
#define AFR_MSG_ENTRY_UNLOCK_FAIL       (GLFS_COMP_BASE_AFR + 7)


/*!
 * @messageid 108008
 * @diagnosis There is an inconsistency in the file's data/metadata/gfid
 * amongst the bricks of a replica set.
 * @recommendedaction Resolve the split brain by clearing the AFR changelog
 * attributes from the appropriate brick and trigger self-heal.
 */
#define AFR_MSG_SPLIT_BRAIN             (GLFS_COMP_BASE_AFR + 8)


/*!
 * @messageid 108009
 * @diagnosis open/opendir failed on a brick.
 * @recommendedaction Error number in the log should give the reason why it
 * failed. Also observe brick logs for more information.
 */
#define AFR_MSG_OPEN_FAIL               (GLFS_COMP_BASE_AFR + 9)


/*!
 * @messageid 108010
 * @diagnosis unlocks failed on a brick.
 * @recommendedaction Error number in the log should give the reason why it
 * failed. Also observe brick logs for more information.
*/
#define AFR_MSG_UNLOCK_FAIL       (GLFS_COMP_BASE_AFR + 10)

/*!
 * @messageid 108011
 * @diagnosis Setting of pending xattrs succeeded/failed during replace-brick
 * operation.
 * @recommendedaction In case of failure, error number in the log should give
 * the reason why it failed. Also observe brick logs for more information.
*/
#define AFR_MSG_REPLACE_BRICK_STATUS     (GLFS_COMP_BASE_AFR + 11)

/*!
 * @messageid 108012
 * @diagnosis
 * @recommendedaction
*/
#define AFR_MSG_GFID_NULL                       (GLFS_COMP_BASE_AFR + 12)

/*!
 * @messageid 108013
 * @diagnosis
 * @recommendedaction
*/
#define AFR_MSG_FD_CREATE_FAILED                (GLFS_COMP_BASE_AFR + 13)

/*!
 * @messageid 108014
 * @diagnosis
 * @recommendedaction
*/
#define AFR_MSG_DICT_SET_FAILED                 (GLFS_COMP_BASE_AFR + 14)

/*!
 * @messageid 108015
 * @diagnosis
 * @recommendedaction
*/
#define AFR_MSG_EXPUNGING_FILE_OR_DIR           (GLFS_COMP_BASE_AFR + 15)

/*!
 * @messageid 108016
 * @diagnosis
 * @recommendedaction
*/
#define AFR_MSG_MIGRATION_IN_PROGRESS           (GLFS_COMP_BASE_AFR + 16)

/*!
 * @messageid 108017
 * @diagnosis
 * @recommendedaction
*/
#define AFR_MSG_CHILD_MISCONFIGURED             (GLFS_COMP_BASE_AFR + 17)

/*!
 * @messageid 108018
 * @diagnosis
 * @recommendedaction
*/
#define AFR_MSG_VOL_MISCONFIGURED               (GLFS_COMP_BASE_AFR + 18)

/*!
 * @messageid 108019
 * @diagnosis
 * @recommendedaction
*/
#define AFR_MSG_BLOCKING_LKS_FAILED             (GLFS_COMP_BASE_AFR + 19)

/*!
 * @messageid 108020
 * @diagnosis
 * @recommendedaction
*/
#define AFR_MSG_INVALID_FD                      (GLFS_COMP_BASE_AFR + 20)

/*!
 * @messageid 108021
 * @diagnosis
 * @recommendedaction
*/
#define AFR_MSG_LOCK_INFO                       (GLFS_COMP_BASE_AFR + 21)

/*!
 * @messageid 108022
 * @diagnosis
 * @recommendedaction
*/
#define AFR_MSG_LOCK_XLATOR_NOT_LOADED          (GLFS_COMP_BASE_AFR + 22)

/*!
 * @messageid 108023
 * @diagnosis
 * @recommendedaction
*/
#define AFR_MSG_FD_CTX_GET_FAILED               (GLFS_COMP_BASE_AFR + 23)

/*!
 * @messageid 108024
 * @diagnosis
 * @recommendedaction
*/
#define AFR_MSG_INVALID_SUBVOL                    (GLFS_COMP_BASE_AFR + 24)

/*!
 * @messageid 108025
 * @diagnosis
 * @recommendedaction
*/
#define AFR_MSG_PUMP_XLATOR_ERROR               (GLFS_COMP_BASE_AFR + 25)

/*!
 * @messageid 108026
 * @diagnosis
 * @recommendedaction
*/
#define AFR_MSG_SELF_HEAL_INFO                  (GLFS_COMP_BASE_AFR + 26)

/*!
 * @messageid 108027
 * @diagnosis
 * @recommendedaction
*/
#define AFR_MSG_READ_SUBVOL_ERROR               (GLFS_COMP_BASE_AFR + 27)

/*!
 * @messageid 108028
 * @diagnosis
 * @recommendedaction
*/
#define AFR_MSG_DICT_GET_FAILED                 (GLFS_COMP_BASE_AFR + 28)


/*!
 * @messageid 108029
 * @diagnosis
 * @recommendedaction
*/
#define AFR_MSG_INFO_COMMON                     (GLFS_COMP_BASE_AFR + 29)

/*!
 * @messageid 108030
 * @diagnosis
 * @recommendedaction
*/
#define AFR_MSG_SPLIT_BRAIN_CHOICE_ERROR         (GLFS_COMP_BASE_AFR + 30)

/*!
 * @messageid 108031
 * @diagnosis
 * @recommendedaction
*/
#define AFR_MSG_LOCAL_CHILD         (GLFS_COMP_BASE_AFR + 31)

/*!
 * @messageid 108032
 * @diagnosis
 * @recommendedaction
*/
#define AFR_MSG_INVALID_DATA         (GLFS_COMP_BASE_AFR + 32)

/*!
 * @messageid 108033
 * @diagnosis
 * @recommendedaction
*/
#define AFR_MSG_INVALID_ARG         (GLFS_COMP_BASE_AFR + 33)

/*!
 * @messageid 108034
 * @diagnosis
 * @recommendedaction
*/
#define AFR_MSG_INDEX_DIR_GET_FAILED         (GLFS_COMP_BASE_AFR + 34)

/*!
 * @messageid 108035
 * @diagnosis
 * @recommendedaction
*/
#define AFR_MSG_FSYNC_FAILED         (GLFS_COMP_BASE_AFR + 35)

/*!
 * @messageid 108036
 * @diagnosis
 * @recommendedaction
*/
#define AFR_MSG_FAVORITE_CHILD         (GLFS_COMP_BASE_AFR + 36)
/*!
 * @messageid 108037
 * @diagnosis
 * @recommendedaction
*/
#define AFR_MSG_SELF_HEAL_FAILED                (GLFS_COMP_BASE_AFR + 37)

/*!
 * @messageid 108038
 * @diagnosis
 * @recommendedaction
*/
#define AFR_MSG_SPLIT_BRAIN_STATUS      (GLFS_COMP_BASE_AFR + 38)

/*!
 * @messageid 108039
 * @diagnosis Setting of pending xattrs succeeded/failed during add-brick
 * operation.
 * @recommendedaction In case of failure, error number in the log should give
 * the reason why it failed. Also observe brick logs for more information.
*/
#define AFR_MSG_ADD_BRICK_STATUS        (GLFS_COMP_BASE_AFR + 39)


/*!
 * @messageid 108040
 * @diagnosis AFR was unable to be loaded because the pending-changelog xattrs
 * were not found in the volfile.
 * @recommendedaction Please ensure cluster op-version is atleast 30707 and the
 * volfiles are regenerated.
*/
#define AFR_MSG_NO_CHANGELOG  (GLFS_COMP_BASE_AFR + 40)

/*!
 * @messageid 108041
 * @diagnosis Unable to create timer thread for delayed initialization.
 * @recommendedaction Possibly check process's log file for messages from
 * timer infra.
*/
#define AFR_MSG_TIMER_CREATE_FAIL               (GLFS_COMP_BASE_AFR + 41)

/*!
 * @messageid 108042
 * @diagnosis Log messages relating to automated resolution of split-brain files
 * based on favorite child policies.
 * @recommendedaction
*/
#define AFR_MSG_SBRAIN_FAV_CHILD_POLICY  (GLFS_COMP_BASE_AFR + 42)

#define glfs_msg_end_x GLFS_MSGID_END, "Invalid: End of messages"
#endif /* !_AFR_MESSAGES_H_ */
