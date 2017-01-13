/*
  Copyright (c) 2015 Red Hat, Inc. <http://www.redhat.com>
  This file is part of GlusterFS.

  This file is licensed to you under your choice of the GNU Lesser
  General Public License, version 3 or any later version (LGPLv3 or
  later), or the GNU General Public License, version 2 (GPLv2), in all
  cases as published by the Free Software Foundation.
*/

#ifndef _EC_MESSAGES_H_
#define _EC_MESSAGES_H_

#ifndef _CONFIG_H
#define _CONFIG_H
#include "config.h"
#endif

#include "glfs-message-id.h"

/*! \file ec-messages.h
 *  \brief Glusterd log-message IDs and their descriptions
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

#define GLFS_EC_COMP_BASE       GLFS_MSGID_COMP_EC
#define GLFS_NUM_MESSAGES       75
#define GLFS_MSGID_END          (GLFS_EC_COMP_BASE + GLFS_NUM_MESSAGES + 1)
/* Messaged with message IDs */
#define glfs_msg_start_x GLFS_EC_COMP_BASE, "Invalid: Start of messages"
/*------------*/

/*!
 * @messageid
 * @diagnosis
 * @recommendedaction
 */
#define EC_MSG_INVALID_CONFIG           (GLFS_EC_COMP_BASE + 1)

/*!
 * @messageid
 * @diagnosis
 * @recommendedaction
 */
#define EC_MSG_HEAL_FAIL                (GLFS_EC_COMP_BASE + 2)

/*!
 * @messageid
 * @diagnosis
 * @recommendedaction
 */
#define EC_MSG_DICT_COMBINE_FAIL        (GLFS_EC_COMP_BASE + 3)

/*!
 * @messageid
 * @diagnosis
 * @recommendedaction
 */
#define EC_MSG_STIME_COMBINE_FAIL       (GLFS_EC_COMP_BASE + 4)

/*!
 * @messageid
 * @diagnosis
 * @recommendedaction
 */
#define EC_MSG_INVALID_DICT_NUMS        (GLFS_EC_COMP_BASE + 5)

/*!
 * @messageid
 * @diagnosis
 * @recommendedaction
 */
#define EC_MSG_IATT_COMBINE_FAIL        (GLFS_EC_COMP_BASE + 6)

/*!
 * @messageid
 * @diagnosis
 * @recommendedaction
 */
#define EC_MSG_INVALID_FORMAT           (GLFS_EC_COMP_BASE + 7)

/*!
 * @messageid
 * @diagnosis
 * @recommendedaction
 */
#define EC_MSG_DICT_GET_FAILED          (GLFS_EC_COMP_BASE + 8)

/*!
 * @messageid
 * @diagnosis
 * @recommendedaction
 */
#define EC_MSG_UNHANDLED_STATE          (GLFS_EC_COMP_BASE + 9)

/*!
 * @messageid
 * @diagnosis
 * @recommendedaction
 */
#define EC_MSG_FILE_DESC_REF_FAIL       (GLFS_EC_COMP_BASE + 10)

/*!
 * @messageid
 * @diagnosis
 * @recommendedaction
 */
#define EC_MSG_LOC_COPY_FAIL            (GLFS_EC_COMP_BASE + 11)

/*!
 * @messageid
 * @diagnosis
 * @recommendedaction
 */
#define EC_MSG_BUF_REF_FAIL             (GLFS_EC_COMP_BASE + 12)

/*!
 * @messageid
 * @diagnosis
 * @recommendedaction
 */
#define EC_MSG_DICT_REF_FAIL            (GLFS_EC_COMP_BASE + 13)

/*!
 * @messageid
 * @diagnosis
 * @recommendedaction
 */
#define EC_MSG_LK_UNLOCK_FAILED         (GLFS_EC_COMP_BASE + 14)

/*!
 * @messageid
 * @diagnosis
 * @recommendedaction
 */
#define EC_MSG_UNLOCK_FAILED            (GLFS_EC_COMP_BASE + 15)

/*!
 * @messageid
 * @diagnosis
 * @recommendedaction
 */
#define EC_MSG_LOC_PARENT_INODE_MISSING         (GLFS_EC_COMP_BASE + 16)

/*!
 * @messageid
 * @diagnosis
 * @recommendedaction
 */
#define EC_MSG_INVALID_LOC_NAME         (GLFS_EC_COMP_BASE + 17)

/*!
 * @messageid
 * @diagnosis
 * @recommendedaction
 */
#define EC_MSG_NO_MEMORY                (GLFS_EC_COMP_BASE + 18)

/*!
 * @messageid
 * @diagnosis
 * @recommendedaction
 */
#define EC_MSG_GFID_MISMATCH            (GLFS_EC_COMP_BASE + 19)

/*!
 * @messageid
 * @diagnosis
 * @recommendedaction
 */
#define EC_MSG_UNSUPPORTED_VERSION      (GLFS_EC_COMP_BASE + 20)

/*!
 * @messageid
 * @diagnosis
 * @recommendedaction
 */
#define EC_MSG_FD_CREATE_FAIL           (GLFS_EC_COMP_BASE + 21)

/*!
 * @messageid
 * @diagnosis
 * @recommendedaction
 */
#define EC_MSG_READDIRP_REQ_PREP_FAIL   (GLFS_EC_COMP_BASE + 22)

/*!
 * @messageid
 * @diagnosis
 * @recommendedaction
 */
#define EC_MSG_LOOKUP_REQ_PREP_FAIL     (GLFS_EC_COMP_BASE + 23)

/*!
 * @messageid
 * @diagnosis
 * @recommendedaction
 */
#define EC_MSG_INODE_REF_FAIL           (GLFS_EC_COMP_BASE + 24)

/*!
 * @messageid
 * @diagnosis
 * @recommendedaction
 */
#define EC_MSG_LOOKUP_READAHEAD_FAIL    (GLFS_EC_COMP_BASE + 25)

/*!
 * @messageid
 * @diagnosis
 * @recommendedaction
 */
#define EC_MSG_FRAME_MISMATCH           (GLFS_EC_COMP_BASE + 26)

/*!
 * @messageid
 * @diagnosis
 * @recommendedaction
 */
#define EC_MSG_XLATOR_MISMATCH          (GLFS_EC_COMP_BASE + 27)

/*!
 * @messageid
 * @diagnosis
 * @recommendedaction
 */
#define EC_MSG_VECTOR_MISMATCH          (GLFS_EC_COMP_BASE + 28)

/*!
 * @messageid
 * @diagnosis
 * @recommendedaction
 */
#define EC_MSG_IATT_MISMATCH            (GLFS_EC_COMP_BASE + 29)

/*!
 * @messageid
 * @diagnosis
 * @recommendedaction
 */
#define EC_MSG_FD_MISMATCH              (GLFS_EC_COMP_BASE + 30)

/*!
 * @messageid
 * @diagnosis
 * @recommendedaction
 */
#define EC_MSG_DICT_MISMATCH            (GLFS_EC_COMP_BASE + 31)

/*!
 * @messageid
 * @diagnosis
 * @recommendedaction
 */
#define EC_MSG_INDEX_DIR_GET_FAIL       (GLFS_EC_COMP_BASE + 32)

/*!
 * @messageid
 * @diagnosis
 * @recommendedaction
 */
#define EC_MSG_PREOP_LOCK_FAILED        (GLFS_EC_COMP_BASE + 33)

/*!
 * @messageid
 * @diagnosis
 * @recommendedaction
 */
#define EC_MSG_CHILDS_INSUFFICIENT      (GLFS_EC_COMP_BASE + 34)

/*!
 * @messageid
 * @diagnosis
 * @recommendedaction
 */
#define EC_MSG_OP_EXEC_UNAVAIL          (GLFS_EC_COMP_BASE + 35)

/*!
 * @messageid
 * @diagnosis
 * @recommendedaction
 */
#define EC_MSG_UNLOCK_DELAY_FAILED      (GLFS_EC_COMP_BASE + 36)

/*!
 * @messageid
 * @diagnosis
 * @recommendedaction
 */
#define EC_MSG_SIZE_VERS_UPDATE_FAIL    (GLFS_EC_COMP_BASE + 37)

/*!
 * @messageid
 * @diagnosis
 * @recommendedaction
 */
#define EC_MSG_INVALID_REQUEST          (GLFS_EC_COMP_BASE + 38)

/*!
 * @messageid
 * @diagnosis
 * @recommendedaction
 */
#define EC_MSG_INVALID_LOCK_TYPE        (GLFS_EC_COMP_BASE + 39)

/*!
 * @messageid
 * @diagnosis
 * @recommendedaction
 */
#define EC_MSG_SIZE_VERS_GET_FAIL       (GLFS_EC_COMP_BASE + 40)

/*!
 * @messageid
 * @diagnosis
 * @recommendedaction
 */
#define EC_MSG_FILE_SIZE_GET_FAIL       (GLFS_EC_COMP_BASE + 41)

/*!
 * @messageid
 * @diagnosis
 * @recommendedaction
 */
#define EC_MSG_FOP_MISMATCH             (GLFS_EC_COMP_BASE + 42)

/*!
 * @messageid
 * @diagnosis
 * @recommendedaction
 */
#define EC_MSG_SUBVOL_ID_DICT_SET_FAIL          (GLFS_EC_COMP_BASE + 43)

/*!
 * @messageid
 * @diagnosis
 * @recommendedaction
 */
#define EC_MSG_SUBVOL_BUILD_FAIL        (GLFS_EC_COMP_BASE + 44)

/*!
 * @messageid
 * @diagnosis
 * @recommendedaction
 */
#define EC_MSG_XLATOR_INIT_FAIL         (GLFS_EC_COMP_BASE + 45)

/*!
 * @messageid
 * @diagnosis
 * @recommendedaction
 */
#define EC_MSG_NO_PARENTS               (GLFS_EC_COMP_BASE + 46)

/*!
 * @messageid
 * @diagnosis
 * @recommendedaction
 */
#define EC_MSG_TIMER_CREATE_FAIL        (GLFS_EC_COMP_BASE + 47)

/*!
 * @messageid
 * @diagnosis
 * @recommendedaction
 */
#define EC_MSG_TOO_MANY_SUBVOLS         (GLFS_EC_COMP_BASE + 48)

/*!
 * @messageid
 * @diagnosis
 * @recommendedaction
 */
#define EC_MSG_DATA_UNAVAILABLE         (GLFS_EC_COMP_BASE + 49)

/*!
 * @messageid
 * @diagnosis
 * @recommendedaction
 */
#define EC_MSG_INODE_REMOVE_FAIL        (GLFS_EC_COMP_BASE + 50)

/*!
 * @messageid
 * @diagnosis
 * @recommendedaction
 */
#define EC_MSG_INVALID_REDUNDANCY        (GLFS_EC_COMP_BASE + 51)

/*!
 * @messageid
 * @diagnosis
 * @recommendedaction
 */
#define EC_MSG_XLATOR_PARSE_OPT_FAIL        (GLFS_EC_COMP_BASE + 52)

/*!
 * @messageid
 * @diagnosis
 * @recommendedaction
 */
#define EC_MSG_OP_FAIL_ON_SUBVOLS         (GLFS_EC_COMP_BASE + 53)

/*!
 * @messageid
 * @diagnosis
 * @recommendedaction
 */
#define EC_MSG_INVALID_INODE             (GLFS_EC_COMP_BASE + 54)

/*!
 * @messageid
 * @diagnosis
 * @recommendedaction
 */
#define EC_MSG_LOCK_MISMATCH             (GLFS_EC_COMP_BASE + 55)

/*!
 * @messageid
 * @diagnosis
 * @recommendedaction
 */
#define EC_MSG_XDATA_MISMATCH             (GLFS_EC_COMP_BASE + 56)

/*!
 * @messageid
 * @diagnosis
 * @recommendedaction
 */
#define EC_MSG_HEALING_INFO               (GLFS_EC_COMP_BASE + 57)

/*!
 * @messageid
 * @diagnosis
 * @recommendedaction
 */
#define EC_MSG_HEAL_SUCCESS               (GLFS_EC_COMP_BASE + 58)

/*!
 * @messageid
 * @diagnosis
 * @recommendedaction
 */
#define EC_MSG_FULL_SWEEP_START           (GLFS_EC_COMP_BASE + 59)

/*!
 * @messageid
 * @diagnosis
 * @recommendedaction
 */
#define EC_MSG_FULL_SWEEP_STOP            (GLFS_EC_COMP_BASE + 59)

/*!
 * @messageid
 * @diagnosis
 * @recommendedaction
 */
#define EC_MSG_INVALID_FOP                 (GLFS_EC_COMP_BASE + 60)

/*!
 * @messageid
 * @diagnosis
 * @recommendedaction
 */
#define EC_MSG_EC_UP                       (GLFS_EC_COMP_BASE + 61)

/*!
 * @messageid
 * @diagnosis
 * @recommendedaction
 */
#define EC_MSG_EC_DOWN                     (GLFS_EC_COMP_BASE + 62)

/*!
 * @messageid
 * @diagnosis
 * @recommendedaction
 */
#define EC_MSG_SIZE_XATTR_GET_FAIL         (GLFS_EC_COMP_BASE + 63)

/*!
 * @messageid
 * @diagnosis
 * @recommendedaction
 */
#define EC_MSG_VER_XATTR_GET_FAIL           (GLFS_EC_COMP_BASE + 64)

/*!
 * @messageid
 * @diagnosis
 * @recommendedaction
 */
#define EC_MSG_CONFIG_XATTR_GET_FAIL        (GLFS_EC_COMP_BASE + 65)

/*!
 * @messageid
 * @diagnosis
 * @recommendedaction
 */
#define EC_MSG_CONFIG_XATTR_INVALID         (GLFS_EC_COMP_BASE + 66)

/*!
 * @messageid
 * @diagnosis
 * @recommendedaction
 */
#define EC_MSG_EXTENSION                    (GLFS_EC_COMP_BASE + 67)

/*!
 * @messageid
 * @diagnosis
 * @recommendedaction
 */
#define EC_MSG_EXTENSION_NONE               (GLFS_EC_COMP_BASE + 68)

/*!
 * @messageid
 * @diagnosis
 * @recommendedaction
 */
#define EC_MSG_EXTENSION_UNKNOWN            (GLFS_EC_COMP_BASE + 69)

/*!
 * @messageid
 * @diagnosis
 * @recommendedaction
 */
#define EC_MSG_EXTENSION_UNSUPPORTED        (GLFS_EC_COMP_BASE + 70)

/*!
 * @messageid
 * @diagnosis
 * @recommendedaction
 */
#define EC_MSG_EXTENSION_FAILED             (GLFS_EC_COMP_BASE + 71)

/*!
 * @messageid
 * @diagnosis
 * @recommendedaction
 */
#define EC_MSG_NO_GF                        (GLFS_EC_COMP_BASE + 72)

/*!
 * @messageid
 * @diagnosis
 * @recommendedaction
 */
#define EC_MSG_MATRIX_FAILED                (GLFS_EC_COMP_BASE + 73)

/*!
 * @messageid
 * @diagnosis
 * @recommendedaction
 */
#define EC_MSG_DYN_CREATE_FAILED            (GLFS_EC_COMP_BASE + 74)

/*!
 * @messageid
 * @diagnosis
 * @recommendedaction
 */
#define EC_MSG_DYN_CODEGEN_FAILED           (GLFS_EC_COMP_BASE + 75)

/*------------*/
#define glfs_msg_end_x GLFS_MSGID_END, "Invalid: End of messages"

#endif /* !_EC_MESSAGES_H_ */
