/*
 Copyright (c) 2013 Red Hat, Inc. <http://www.redhat.com>
 This file is part of GlusterFS.

 This file is licensed to you under your choice of the GNU Lesser
 General Public License, version 3 or any later version (LGPLv3 or
 later), or the GNU General Public License, version 2 (GPLv2), in all
 cases as published by the Free Software Foundation.
 */

#ifndef _component_MESSAGES_H_
#define _component_MESSAGES_H_

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

#define GLFS_COMP_BASE         GLFS_MSGID_COMP_CTR
#define GLFS_NUM_MESSAGES       57
#define GLFS_MSGID_END          (GLFS_COMP_BASE + GLFS_NUM_MESSAGES + 1)
/* Messaged with message IDs */
#define glfs_msg_start_x GLFS_COMP_BASE, "Invalid: Start of messages"
/*------------*/

#define CTR_MSG_CREATE_CTR_LOCAL_ERROR_WIND              (GLFS_COMP_BASE + 1)

/*!
 * @messageid
 * @diagnosis
 * @recommendedaction
 *
 */
#define CTR_MSG_FILL_CTR_LOCAL_ERROR_UNWIND              (GLFS_COMP_BASE + 2)

/*!
 * @messageid
 * @diagnosis
 * @recommendedaction
 *
 */
#define CTR_MSG_FILL_CTR_LOCAL_ERROR_WIND                (GLFS_COMP_BASE + 3)

/*!
 * @messageid
 * @diagnosis
 * @recommendedaction
 *
 */
#define CTR_MSG_INSERT_LINK_WIND_FAILED                  (GLFS_COMP_BASE + 4)

/*!
 * @messageid
 * @diagnosis
 * @recommendedaction
 *
 */
#define CTR_MSG_INSERT_WRITEV_WIND_FAILED                (GLFS_COMP_BASE + 5)

/*!
 * @messageid
 * @diagnosis
 * @recommendedaction
 *
 */
#define CTR_MSG_INSERT_WRITEV_UNWIND_FAILED              (GLFS_COMP_BASE + 6)

/*!
 * @messageid
 * @diagnosis
 * @recommendedaction
 *
 */
#define CTR_MSG_INSERT_SETATTR_WIND_FAILED               (GLFS_COMP_BASE + 7)

/*!
 * @messageid
 * @diagnosis
 * @recommendedaction
 *
 */
#define CTR_MSG_INSERT_SETATTR_UNWIND_FAILED             (GLFS_COMP_BASE + 8)

/*!
 * @messageid
 * @diagnosis
 * @recommendedaction
 *
 */
#define CTR_MSG_INSERT_FREMOVEXATTR_UNWIND_FAILED        (GLFS_COMP_BASE + 9)

/*!
 * @messageid
 * @diagnosis
 * @recommendedaction
 *
 */
#define CTR_MSG_INSERT_FREMOVEXATTR_WIND_FAILED          (GLFS_COMP_BASE + 10)

/*!
 * @messageid
 * @diagnosis
 * @recommendedaction
 *
 */
#define CTR_MSG_INSERT_REMOVEXATTR_WIND_FAILED           (GLFS_COMP_BASE + 11)

/*!
 * @messageid
 * @diagnosis
 * @recommendedaction
 *
 */
#define CTR_MSG_INSERT_REMOVEXATTR_UNWIND_FAILED         (GLFS_COMP_BASE + 12)

/*!
 * @messageid
 * @diagnosis
 * @recommendedaction
 *
 */
#define CTR_MSG_INSERT_TRUNCATE_WIND_FAILED              (GLFS_COMP_BASE + 13)

/*!
 * @messageid
 * @diagnosis
 * @recommendedaction
 *
 */
#define CTR_MSG_INSERT_TRUNCATE_UNWIND_FAILED            (GLFS_COMP_BASE + 14)

/*!
 * @messageid
 * @diagnosis
 * @recommendedaction
 *
 */
#define CTR_MSG_INSERT_FTRUNCATE_UNWIND_FAILED           (GLFS_COMP_BASE + 15)

/*!
 * @messageid
 * @diagnosis
 * @recommendedaction
 *
 */
#define CTR_MSG_INSERT_FTRUNCATE_WIND_FAILED             (GLFS_COMP_BASE + 16)

/*!
 * @messageid
 * @diagnosis
 * @recommendedaction
 *
 */
#define CTR_MSG_INSERT_RENAME_WIND_FAILED                (GLFS_COMP_BASE + 17)

/*!
 * @messageid
 * @diagnosis
 * @recommendedaction
 *
 */
#define CTR_MSG_INSERT_RENAME_UNWIND_FAILED              (GLFS_COMP_BASE + 18)

/*!
 * @messageid
 * @diagnosis
 * @recommendedaction
 *
 */
#define CTR_MSG_ACCESS_CTR_INODE_CONTEXT_FAILED          (GLFS_COMP_BASE + 19)

/*!
 * @messageid
 * @diagnosis
 * @recommendedaction
 *
 */
#define CTR_MSG_ADD_HARDLINK_FAILED                      (GLFS_COMP_BASE + 20)

/*!
 * @messageid
 * @diagnosis
 * @recommendedaction
 *
 */
#define CTR_MSG_DELETE_HARDLINK_FAILED                   (GLFS_COMP_BASE + 21)

/*!
 * @messageid
 * @diagnosis
 * @recommendedaction
 *
 */
#define CTR_MSG_UPDATE_HARDLINK_FAILED                   (GLFS_COMP_BASE + 22)

/*!
 * @messageid
 * @diagnosis
 * @recommendedaction
 *
 */
#define CTR_MSG_GET_CTR_RESPONSE_LINK_COUNT_XDATA_FAILED (GLFS_COMP_BASE + 23)

/*!
 * @messageid
 * @diagnosis
 * @recommendedaction
 *
 */
#define CTR_MSG_SET_CTR_RESPONSE_LINK_COUNT_XDATA_FAILED (GLFS_COMP_BASE + 24)

/*!
 * @messageid
 * @diagnosis
 * @recommendedaction
 *
 */
#define CTR_MSG_INSERT_UNLINK_UNWIND_FAILED              (GLFS_COMP_BASE + 25)

/*!
 * @messageid
 * @diagnosis
 * @recommendedaction
 *
 */
#define CTR_MSG_INSERT_UNLINK_WIND_FAILED                (GLFS_COMP_BASE + 26)

/*!
 * @messageid
 * @diagnosis
 * @recommendedaction
 *
 */
#define CTR_MSG_XDATA_NULL                               (GLFS_COMP_BASE + 27)

/*!
 * @messageid
 * @diagnosis
 * @recommendedaction
 *
 */
#define CTR_MSG_INSERT_FSYNC_WIND_FAILED                 (GLFS_COMP_BASE + 28)

/*!
 * @messageid
 * @diagnosis
 * @recommendedaction
 *
 */
#define CTR_MSG_INSERT_FSYNC_UNWIND_FAILED               (GLFS_COMP_BASE + 29)

/*!
 * @messageid
 * @diagnosis
 * @recommendedaction
 *
 */
#define CTR_MSG_INSERT_MKNOD_UNWIND_FAILED               (GLFS_COMP_BASE + 30)

/*!
 * @messageid
 * @diagnosis
 * @recommendedaction
 *
 */
#define CTR_MSG_INSERT_MKNOD_WIND_FAILED                 (GLFS_COMP_BASE + 31)

/*!
 * @messageid
 * @diagnosis
 * @recommendedaction
 *
 */
#define CTR_MSG_INSERT_CREATE_WIND_FAILED                (GLFS_COMP_BASE + 32)

/*!
 * @messageid
 * @diagnosis
 * @recommendedaction
 *
 */
#define CTR_MSG_INSERT_CREATE_UNWIND_FAILED              (GLFS_COMP_BASE + 33)

/*!
 * @messageid
 * @diagnosis
 * @recommendedaction
 *
 */
#define CTR_MSG_INSERT_RECORD_WIND_FAILED                (GLFS_COMP_BASE + 34)

/*!
 * @messageid
 * @diagnosis
 * @recommendedaction
 *
 */
#define CTR_MSG_INSERT_READV_WIND_FAILED                 (GLFS_COMP_BASE + 35)

/*!
 * @messageid
 * @diagnosis
 * @recommendedaction
 *
 */
#define CTR_MSG_GET_GFID_FROM_DICT_FAILED                (GLFS_COMP_BASE + 36)

/*!
 * @messageid
 * @diagnosis
 * @recommendedaction
 *
 */
#define CTR_MSG_SET                                      (GLFS_COMP_BASE + 37)

/*!
 * @messageid
 * @diagnosis
 * @recommendedaction
 *
 */
#define CTR_MSG_FATAL_ERROR                              (GLFS_COMP_BASE + 38)

/*!
 * @messageid
 * @diagnosis
 * @recommendedaction
 *
 */
#define CTR_MSG_DANGLING_VOLUME                          (GLFS_COMP_BASE + 39)

/*!
 * @messageid
 * @diagnosis
 * @recommendedaction
 *
 */
#define CTR_MSG_CALLOC_FAILED                            (GLFS_COMP_BASE + 40)

/*!
 * @messageid
 * @diagnosis
 * @recommendedaction
 *
 */
#define CTR_MSG_EXTRACT_CTR_XLATOR_OPTIONS_FAILED        (GLFS_COMP_BASE + 41)

/*!
 * @messageid
 * @diagnosis
 * @recommendedaction
 *
 */
#define CTR_MSG_INIT_DB_PARAMS_FAILED                    (GLFS_COMP_BASE + 42)

/*!
 * @messageid
 * @diagnosis
 * @recommendedaction
 *
 */
#define CTR_MSG_CREATE_LOCAL_MEMORY_POOL_FAILED          (GLFS_COMP_BASE + 43)

/*!
 * @messageid
 * @diagnosis
 * @recommendedaction
 *
 */
#define CTR_MSG_MEM_ACC_INIT_FAILED                      (GLFS_COMP_BASE + 44)

/*!
 * @messageid
 * @diagnosis
 * @recommendedaction
 *
 */
#define CTR_MSG_CLOSE_DB_CONN_FAILED                     (GLFS_COMP_BASE + 45)

/*!
 * @messageid
 * @diagnosis
 * @recommendedaction
 *
 */
#define CTR_MSG_FILL_UNWIND_TIME_REC_ERROR               (GLFS_COMP_BASE + 46)

/*!
 * @messageid
 * @diagnosis
 * @recommendedaction
 *
 */
#define CTR_MSG_WRONG_FOP_PATH                           (GLFS_COMP_BASE + 47)

/*!
 * @messageid
 * @diagnosis
 * @recommendedaction
 *
 */
#define CTR_MSG_CONSTRUCT_DB_PATH_FAILED                 (GLFS_COMP_BASE + 48)

/*!
 * @messageid
 * @diagnosis
 * @recommendedaction
 *
 */
#define CTR_MSG_SET_VALUE_TO_SQL_PARAM_FAILED            (GLFS_COMP_BASE + 49)

/*!
 * @messageid
 * @diagnosis
 * @recommendedaction
 *
 */
#define CTR_MSG_XLATOR_DISABLED                          (GLFS_COMP_BASE + 50)

/*!
 * @messageid
 * @diagnosis
 * @recommendedaction
 *
 */
#define CTR_MSG_HARDLINK_MISSING_IN_LIST                 (GLFS_COMP_BASE + 51)

/*!
 * @messageid
 * @diagnosis
 * @recommendedaction
 *
 */
#define CTR_MSG_ADD_HARDLINK_TO_LIST_FAILED              (GLFS_COMP_BASE + 52)

/*!
 * @messageid
 * @diagnosis
 * @recommendedaction
 *
 */
#define CTR_MSG_INIT_LOCK_FAILED                         (GLFS_COMP_BASE + 53)

/*!
 * @messageid
 * @diagnosis
 * @recommendedaction
 *
 */
#define CTR_MSG_COPY_FAILED                              (GLFS_COMP_BASE + 54)

/*!
 * @messageid
 * @diagnosis
 * @recommendedaction
 *
 */
#define CTR_MSG_EXTRACT_DB_PARAM_OPTIONS_FAILED          (GLFS_COMP_BASE + 55)

/*!
 * @messageid
 * @diagnosis
 * @recommendedaction
 *
 */
#define CTR_MSG_ADD_HARDLINK_TO_CTR_INODE_CONTEXT_FAILED (GLFS_COMP_BASE + 56)

/*!
 * @messageid
 * @diagnosis
 * @recommendedaction
 *
 */
#define CTR_MSG_NULL_LOCAL                               (GLFS_COMP_BASE + 57)
/*------------*/
#define glfs_msg_end_x GLFS_MSGID_END, "Invalid: End of messages"

#endif /* !_component_MESSAGES_H_ */
