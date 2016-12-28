/*
  Copyright (c) 2015 Red Hat, Inc. <http://www.redhat.com>
  This file is part of GlusterFS.

  This file is licensed to you under your choice of the GNU Lesser
  General Public License, version 3 or any later version (LGPLv3 or
  later), or the GNU General Public License, version 2 (GPLv2), in all
  cases as published by the Free Software Foundation.
*/

#ifndef _PS_MESSAGES_H__
#define _PS_MESSAGES_H__

#include "glfs-message-id.h"

/*! \file server-messages.h
 *  \brief server log-message IDs and their descriptions
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

#define GLFS_PS_BASE                GLFS_MSGID_COMP_PS
#define GLFS_NUM_MESSAGES           91
#define GLFS_MSGID_END              (GLFS_PS_BASE + GLFS_NUM_MESSAGES + 1)
/* Messages with message IDs */
#define glfs_msg_start_x GLFS_PS_BASE, "Invalid: Start of messages"
/*------------*/

#define PS_MSG_AUTHENTICATE_ERROR               (GLFS_PS_BASE + 1)

/*!
 * @messageid
 * @diagnosis
 * @recommendedaction
 *
 */

#define PS_MSG_VOL_VALIDATE_FAILED              (GLFS_PS_BASE + 2)

/*!
 * @messageid
 * @diagnosis
 * @recommendedaction
 *
 */

#define PS_MSG_AUTH_INIT_FAILED                 (GLFS_PS_BASE + 3)

/*!
 * @messageid
 * @diagnosis
 * @recommendedaction
 *
 */

#define PS_MSG_REMOTE_CLIENT_REFUSED            (GLFS_PS_BASE + 4)

/*!
 * @messageid
 * @diagnosis
 * @recommendedaction
 *
 */

#define PS_MSG_GFID_RESOLVE_FAILED              (GLFS_PS_BASE + 5)

/*!
 * @messageid
 * @diagnosis
 * @recommendedaction
 *
 */

#define PS_MSG_ANONYMOUS_FD_CREATE_FAILED       (GLFS_PS_BASE + 6)

/*!
 * @messageid
 * @diagnosis
 * @recommendedaction
 *
 */

#define PS_MSG_NO_MEMORY                        (GLFS_PS_BASE + 7)

/*!
 * @messageid
 * @diagnosis
 * @recommendedaction
 *
 */

#define PS_MSG_FD_NOT_FOUND                     (GLFS_PS_BASE + 8)

/*!
 * @messageid
 * @diagnosis
 * @recommendedaction
 *
 */

#define PS_MSG_INVALID_ENTRY                    (GLFS_PS_BASE + 9)

/*!
 * @messageid
 * @diagnosis
 * @recommendedaction
 *
 */

#define PS_MSG_GET_UID_FAILED                   (GLFS_PS_BASE + 10)

/*!
 * @messageid
 * @diagnosis
 * @recommendedaction
 *
 */

#define PS_MSG_UID_NOT_FOUND                    (GLFS_PS_BASE + 11)

/*!
 * @messageid
 * @diagnosis
 * @recommendedaction
 *
 */

#define PS_MSG_MAPPING_ERROR                    (GLFS_PS_BASE + 12)

/*!
 * @messageid
 * @diagnosis
 * @recommendedaction
 *
 */

#define PS_MSG_FD_CLEANUP                       (GLFS_PS_BASE + 13)

/*!
 * @messageid
 * @diagnosis
 * @recommendedaction
 *
 */

#define PS_MSG_SERVER_CTX_GET_FAILED            (GLFS_PS_BASE + 14)

/*!
 * @messageid
 * @diagnosis
 * @recommendedaction
 *
 */

#define PS_MSG_FDENTRY_NULL                     (GLFS_PS_BASE + 15)

/*!
 * @messageid
 * @diagnosis
 * @recommendedaction
 *
 */

#define PS_MSG_DIR_NOT_FOUND                    (GLFS_PS_BASE + 16)

/*!
 * @messageid
 * @diagnosis
 * @recommendedaction
 *
 */

#define PS_MSG_SERVER_MSG                       (GLFS_PS_BASE + 17)

/*!
 * @messageid
 * @diagnosis
 * @recommendedaction
 *
 */

#define PS_MSG_DICT_SERIALIZE_FAIL              (GLFS_PS_BASE + 18)

/*!
 * @messageid
 * @diagnosis
 * @recommendedaction
 *
 */

#define PS_MSG_RW_STAT                          (GLFS_PS_BASE + 19)

/*!
 * @messageid
 * @diagnosis
 * @recommendedaction
 *
 */

#define PS_MSG_DICT_GET_FAILED                  (GLFS_PS_BASE + 20)

/*!
 * @messageid
 * @diagnosis
 * @recommendedaction
 *
 */

#define PS_MSG_LOGIN_ERROR                      (GLFS_PS_BASE + 21)

/*!
 * @messageid
 * @diagnosis
 * @recommendedaction
 *
 */

#define PS_MSG_REMOUNT_CLIENT_REQD              (GLFS_PS_BASE + 22)

/*!
 * @messageid
 * @diagnosis
 * @recommendedaction
 *
 */

#define PS_MSG_DEFAULTING_FILE                  (GLFS_PS_BASE + 23)

/*!
 * @messageid
 * @diagnosis
 * @recommendedaction
 *
 */

#define PS_MSG_VOL_FILE_OPEN_FAILED             (GLFS_PS_BASE + 24)

/*!
 * @messageid
 * @diagnosis
 * @recommendedaction
 *
 */

#define PS_MSG_STAT_ERROR                       (GLFS_PS_BASE + 25)

/*!
 * @messageid
 * @diagnosis
 * @recommendedaction
 *
 */

#define PS_MSG_SSL_NAME_SET_FAILED              (GLFS_PS_BASE + 26)

/*!
 * @messageid
 * @diagnosis
 * @recommendedaction
 *
 */

#define PS_MSG_ASPRINTF_FAILED                  (GLFS_PS_BASE + 27)

/*!
 * @messageid
 * @diagnosis
 * @recommendedaction
 *
 */

#define PS_MSG_CLIENT_VERSION_NOT_SET           (GLFS_PS_BASE + 28)

/*!
 * @messageid
 * @diagnosis
 * @recommendedaction
 *
 */

#define PS_MSG_CLIENT_ACCEPTED                  (GLFS_PS_BASE + 29)

/*!
 * @messageid
 * @diagnosis
 * @recommendedaction
 *
 */

#define PS_MSG_CLIENT_LK_VERSION_ERROR          (GLFS_PS_BASE + 30)

/*!
 * @messageid
 * @diagnosis
 * @recommendedaction
 *
 */

#define PS_MSG_GRACE_TIMER_EXPD              (GLFS_PS_BASE + 31)

/*!
 * @messageid
 * @diagnosis
 * @recommendedaction
 *
 */

#define PS_MSG_SERIALIZE_REPLY_FAILED           (GLFS_PS_BASE + 32)

/*!
 * @messageid
 * @diagnosis
 * @recommendedaction
 *
 */

#define PS_MSG_AUTH_IP_ERROR                    (GLFS_PS_BASE + 33)

/*!
 * @messageid
 * @diagnosis
 * @recommendedaction
 *
 */

#define PS_MSG_SKIP_FORMAT_CHK                  (GLFS_PS_BASE + 34)

/*!
 * @messageid
 * @diagnosis
 * @recommendedaction
 *
 */

#define PS_MSG_INTERNET_ADDR_ERROR              (GLFS_PS_BASE + 35)

/*!
 * @messageid
 * @diagnosis
 * @recommendedaction
 *
 */

#define PS_MSG_CLIENT_DISCONNECTING             (GLFS_PS_BASE + 36)

/*!
 * @messageid
 * @diagnosis
 * @recommendedaction
 *
 */

#define PS_MSG_GRACE_TIMER_START                (GLFS_PS_BASE + 37)

/*!
 * @messageid
 * @diagnosis
 * @recommendedaction
 *
 */

#define PS_MSG_STATEDUMP_PATH_ERROR             (GLFS_PS_BASE + 38)

/*!
 * @messageid
 * @diagnosis
 * @recommendedaction
 *
 */

#define PS_MSG_GRP_CACHE_ERROR                  (GLFS_PS_BASE + 39)

/*!
 * @messageid
 * @diagnosis
 * @recommendedaction
 *
 */

#define PS_MSG_RPC_CONF_ERROR                   (GLFS_PS_BASE + 40)

/*!
 * @messageid
 * @diagnosis
 * @recommendedaction
 *
 */

#define PS_MSG_TRANSPORT_ERROR                  (GLFS_PS_BASE + 41)

/*!
 * @messageid
 * @diagnosis
 * @recommendedaction
 *
 */

#define PS_MSG_SUBVOL_NULL                      (GLFS_PS_BASE + 42)

/*!
 * @messageid
 * @diagnosis
 * @recommendedaction
 *
 */

#define PS_MSG_PARENT_VOL_ERROR                 (GLFS_PS_BASE + 43)

/*!
 * @messageid
 * @diagnosis
 * @recommendedaction
 *
 */

#define PS_MSG_RPCSVC_CREATE_FAILED             (GLFS_PS_BASE + 44)

/*!
 * @messageid
 * @diagnosis
 * @recommendedaction
 *
 */

#define PS_MSG_RPCSVC_LISTENER_CREATE_FAILED    (GLFS_PS_BASE + 45)

/*!
 * @messageid
 * @diagnosis
 * @recommendedaction
 *
 */

#define PS_MSG_RPCSVC_NOTIFY                    (GLFS_PS_BASE + 46)

/*!
 * @messageid
 * @diagnosis
 * @recommendedaction
 *
 */

#define PS_MSG_PGM_REG_FAILED                   (GLFS_PS_BASE + 47)

/*!
 * @messageid
 * @diagnosis
 * @recommendedaction
 *
 */

#define PS_MSG_ULIMIT_SET_FAILED                (GLFS_PS_BASE + 48)

/*!
 * @messageid
 * @diagnosis
 * @recommendedaction
 *
 */

#define PS_MSG_STATFS                           (GLFS_PS_BASE + 49)

/*!
 * @messageid
 * @diagnosis
 * @recommendedaction
 *
 */

#define PS_MSG_LOOKUP_INFO                      (GLFS_PS_BASE + 50)

/*!
 * @messageid
 * @diagnosis
 * @recommendedaction
 *
 */

#define PS_MSG_LK_INFO                          (GLFS_PS_BASE + 51)

/*!
 * @messageid
 * @diagnosis
 * @recommendedaction
 *
 */

#define PS_MSG_LOCK_ERROR                       (GLFS_PS_BASE + 52)

/*!
 * @messageid
 * @diagnosis
 * @recommendedaction
 *
 */

#define PS_MSG_INODELK_INFO                     (GLFS_PS_BASE + 53)

/*!
 * @messageid
 * @diagnosis
 * @recommendedaction
 *
 */

#define PS_MSG_ENTRYLK_INFO                     (GLFS_PS_BASE + 54)

/*!
 * @messageid
 * @diagnosis
 * @recommendedaction
 *
 */

#define PS_MSG_ACCESS_INFO                      (GLFS_PS_BASE + 55)

/*!
 * @messageid
 * @diagnosis
 * @recommendedaction
 *
 */

#define PS_MSG_DIR_INFO                         (GLFS_PS_BASE + 56)

/*!
 * @messageid
 * @diagnosis
 * @recommendedaction
 *
 */

#define PS_MSG_MKNOD_INFO                       (GLFS_PS_BASE + 57)

/*!
 * @messageid
 * @diagnosis
 * @recommendedaction
 *
 */

#define PS_MSG_REMOVEXATTR_INFO                 (GLFS_PS_BASE + 58)

/*!
 * @messageid
 * @diagnosis
 * @recommendedaction
 *
 */

#define PS_MSG_GETXATTR_INFO                    (GLFS_PS_BASE + 59)

/*!
 * @messageid
 * @diagnosis
 * @recommendedaction
 *
 */

#define PS_MSG_SETXATTR_INFO                    (GLFS_PS_BASE + 60)

/*!
 * @messageid
 * @diagnosis
 * @recommendedaction
 *
 */

#define PS_MSG_RENAME_INFO                      (GLFS_PS_BASE + 61)

/*!
 * @messageid
 * @diagnosis
 * @recommendedaction
 *
 */

#define PS_MSG_LINK_INFO                        (GLFS_PS_BASE + 62)

/*!
 * @messageid
 * @diagnosis
 * @recommendedaction
 *
 */

#define PS_MSG_TRUNCATE_INFO                    (GLFS_PS_BASE + 63)

/*!
 * @messageid
 * @diagnosis
 * @recommendedaction
 *
 */

#define PS_MSG_FSTAT_INFO                       (GLFS_PS_BASE + 64)

/*!
 * @messageid
 * @diagnosis
 * @recommendedaction
 *
 */

#define PS_MSG_FLUSH_INFO                       (GLFS_PS_BASE + 65)

/*!
 * @messageid
 * @diagnosis
 * @recommendedaction
 *
 */

#define PS_MSG_SYNC_INFO                        (GLFS_PS_BASE + 66)

/*!
 * @messageid
 * @diagnosis
 * @recommendedaction
 *
 */

#define PS_MSG_WRITE_INFO                       (GLFS_PS_BASE + 67)

/*!
 * @messageid
 * @diagnosis
 * @recommendedaction
 *
 */

#define PS_MSG_READ_INFO                        (GLFS_PS_BASE + 68)

/*!
 * @messageid
 * @diagnosis
 * @recommendedaction
 *
 */

#define PS_MSG_CHKSUM_INFO                      (GLFS_PS_BASE + 69)

/*!
 * @messageid
 * @diagnosis
 * @recommendedaction
 *
 */

#define PS_MSG_OPEN_INFO                        (GLFS_PS_BASE + 70)

/*!
 * @messageid
 * @diagnosis
 * @recommendedaction
 *
 */

#define PS_MSG_CREATE_INFO                      (GLFS_PS_BASE + 71)

/*!
 * @messageid
 * @diagnosis
 * @recommendedaction
 *
 */

#define PS_MSG_SETATTR_INFO                     (GLFS_PS_BASE + 72)

/*!
 * @messageid
 * @diagnosis
 * @recommendedaction
 *
 */

#define PS_MSG_XATTROP_INFO                     (GLFS_PS_BASE + 73)

/*!
 * @messageid
 * @diagnosis
 * @recommendedaction
 *
 */

#define PS_MSG_ALLOC_INFO                       (GLFS_PS_BASE + 74)

/*!
 * @messageid
 * @diagnosis
 * @recommendedaction
 *
 */

#define PS_MSG_DISCARD_INFO                     (GLFS_PS_BASE + 75)

/*!
 * @messageid
 * @diagnosis
 * @recommendedaction
 *
 */

#define PS_MSG_ZEROFILL_INFO                    (GLFS_PS_BASE + 76)

/*!
 * @messageid
 * @diagnosis
 * @recommendedaction
 *
 */

#define PS_MSG_FD_CREATE_FAILED                 (GLFS_PS_BASE + 77)

/*!
 * @messageid
 * @diagnosis
 * @recommendedaction
 *
 */

#define PS_MSG_WRONG_STATE                      (GLFS_PS_BASE + 78)

/*!
 * @messageid
 * @diagnosis
 * @recommendedaction
 *
 */

#define PS_MSG_CONF_DIR_INVALID                 (GLFS_PS_BASE + 79)

/*!
 * @messageid
 * @diagnosis
 * @recommendedaction
 *
 */

#define PS_MSG_MOUNT_PT_FAIL                    (GLFS_PS_BASE + 80)

/*!
 * @messageid
 * @diagnosis
 * @recommendedaction
 *
 */

#define PS_MSG_STAT_INFO                        (GLFS_PS_BASE + 81)

/*!
 * @messageid
 * @diagnosis
 * @recommendedaction
 *
 */

#define PS_MSG_FILE_OP_FAILED                   (GLFS_PS_BASE + 82)

/*!
 * @messageid
 * @diagnosis
 * @recommendedaction
 *
 */

#define PS_MSG_GRACE_TIMER_CANCELLED            (GLFS_PS_BASE + 83)

/*!
 * @messageid
 * @diagnosis
 * @recommendedaction
 *
 */

#define PS_MSG_ENCODE_MSG_FAILED                (GLFS_PS_BASE + 84)

/*!
 * @messageid
 * @diagnosis
 * @recommendedaction
 *
 */

#define PS_MSG_REPLY_SUBMIT_FAILED              (GLFS_PS_BASE + 85)

/*!
 * @messageid
 * @diagnosis
 * @recommendedaction
 *
 */

#define PS_MSG_RPC_NOTIFY_ERROR                  (GLFS_PS_BASE + 86)

/*!
 * @messageid
 * @diagnosis
 * @recommendedaction
 *
 */

#define PS_MSG_SERVER_EVENT_UPCALL_FAILED       (GLFS_PS_BASE + 87)

/*!
 * @messageid
 * @diagnosis
 * @recommendedaction
 *
 */

#define PS_MSG_SERVER_IPC_INFO                  (GLFS_PS_BASE + 88)

/*!
 * @messageid
 * @diagnosis
 * @recommendedaction
 *
 */

#define PS_MSG_SEEK_INFO                        (GLFS_PS_BASE + 89)

/*!
 * @messageid
 * @diagnosis
 * @recommendedaction
 *
 */

#define PS_MSG_COMPOUND_INFO                    (GLFS_PS_BASE + 90)

/*!
 * @messageid
 * @diagnosis
 * @recommendedaction
 *
 */

#define PS_MSG_CLIENT_OPVERSION_GET_FAILED      (GLFS_PS_BASE + 91)
/*------------*/
#define glfs_msg_end_x GLFS_MSGID_END, "Invalid: End of messages"

#endif /* !_PS_MESSAGES_H__ */

