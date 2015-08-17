/*Copyright (c) 2015 Red Hat, Inc. <http://www.redhat.com>
  This file is part of GlusterFS.

  This file is licensed to you under your choice of the GNU Lesser
  General Public License, version 3 or any later version (LGPLv3 or
  later), or the GNU General Public License, version 2 (GPLv2), in all
  cases as published by the Free Software Foundation.
*/

#ifndef _NFS_MESSAGES_H_
#define _NFS_MESSAGES_H_

#include "glfs-message-id.h"

/*! \file nfs-messages.h
 *  \brief NFS log-message IDs and their descriptions
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

#define GLFS_NFS_BASE                   GLFS_MSGID_COMP_NFS
#define GLFS_NFS_NUM_MESSAGES           202
#define GLFS_MSGID_END                  (GLFS_NFS_BASE + GLFS_NFS_NUM_MESSAGES + 1)

/* Messages with message IDs */
#define glfs_msg_start_x GLFS_NFS_BASE, "Invalid: Start of messages"

/*------------*/

#define NFS_MSG_UNUSED_1                (GLFS_NFS_BASE + 1)
/*!
 * @messageid
 * @diagnosis
 * @recommendedaction
 *
 */

#define NFS_MSG_UNUSED_2                (GLFS_NFS_BASE + 2)
/*!
 * @messageid
 * @diagnosis
 * @recommendedaction
 *
 */

#define NFS_MSG_INVALID_ENTRY           (GLFS_NFS_BASE + 3)
/*!
 * @messageid
 * @diagnosis
 * @recommendedaction
 *
 */

#define NFS_MSG_INODE_LOC_FILL_ERROR    (GLFS_NFS_BASE + 4)
/*!
 * @messageid
 * @diagnosis
 * @recommendedaction
 *
 */

#define NFS_MSG_HARD_RESOLVE_FAIL       (GLFS_NFS_BASE + 5)
/*!
 * @messageid
 * @diagnosis
 * @recommendedaction
 *
 */

#define NFS_MSG_ARGS_DECODE_ERROR       (GLFS_NFS_BASE + 6)
/*!
 * @messageid
 * @diagnosis
 * @recommendedaction
 *
 */

#define NFS_MSG_LOOKUP_PROC_FAIL        (GLFS_NFS_BASE + 7)
/*!
 * @messageid
 * @diagnosis
 * @recommendedaction
 *
 */

#define NFS_MSG_UNUSED_8                (GLFS_NFS_BASE + 8)
/*!
 * @messageid
 * @diagnosis
 * @recommendedaction
 *
 */

#define NFS_MSG_UNUSED_9                (GLFS_NFS_BASE + 9)
/*!
 * @messageid
 * @diagnosis
 * @recommendedaction
 *
 */

#define NFS_MSG_READLINK_PROC_FAIL      (GLFS_NFS_BASE + 10)
/*!
 * @messageid
 * @diagnosis
 * @recommendedaction
 *
 */

#define NFS_MSG_UNUSED_11               (GLFS_NFS_BASE + 11)
/*!
 * @messageid
 * @diagnosis
 * @recommendedaction
 *
 */

#define NFS_MSG_ANONYMOUS_FD_FAIL       (GLFS_NFS_BASE + 12)
/*!
 * @messageid
 * @diagnosis
 * @recommendedaction
 *
 */
#define NFS_MSG_READ_FAIL               (GLFS_NFS_BASE + 13)
/*!
 * @messageid
 * @diagnosis
 * @recommendedaction
 *
 */

#define NFS_MSG_UNUSED_14               (GLFS_NFS_BASE + 14)
/*!
 * @messageid
 * @diagnosis
 * @recommendedaction
 *
 */

#define NFS_MSG_UNUSED_15               (GLFS_NFS_BASE + 15)
/*!
 * @messageid
 * @diagnosis
 * @recommendedaction
 *
 */

#define NFS_MSG_STATE_WRONG             (GLFS_NFS_BASE + 16)
/*!
 * @messageid
 * @diagnosis
 * @recommendedaction
 *
 */

#define NFS_MSG_WRITE_FAIL              (GLFS_NFS_BASE + 17)
/*!
 * @messageid
 * @diagnosis
 * @recommendedaction
 *
 */

#define NFS_MSG_UNUSED_18               (GLFS_NFS_BASE + 18)
/*!
 * @messageid
 * @diagnosis
 * @recommendedaction
 *
 */

#define NFS_MSG_UNUSED_19               (GLFS_NFS_BASE + 19)
/*!
 * @messageid
 * @diagnosis
 * @recommendedaction
 *
 */

#define NFS_MSG_UNUSED_20               (GLFS_NFS_BASE + 20)
/*!
 * @messageid
 * @diagnosis
 * @recommendedaction
 *
 */


#define NFS_MSG_CREATE_FAIL             (GLFS_NFS_BASE + 21)
/*!
 * @messageid
 * @diagnosis
 * @recommendedaction
 *
 */

#define NFS_MSG_UNUSED_22               (GLFS_NFS_BASE + 22)
/*!
 * @messageid
 * @diagnosis
 * @recommendedaction
 *
 */

#define NFS_MSG_UNUSED_23               (GLFS_NFS_BASE + 23)
/*!
 * @messageid
 * @diagnosis
 * @recommendedaction
 *
 */

#define NFS_MSG_DIR_OP_FAIL             (GLFS_NFS_BASE + 24)
/*!
 * @messageid
 * @diagnosis
 * @recommendedaction
 *
 */

#define NFS_MSG_UNUSED_25               (GLFS_NFS_BASE + 25)
/*!
 * @messageid
 * @diagnosis
 * @recommendedaction
 *
 */

#define NFS_MSG_SYMLINK_FAIL           (GLFS_NFS_BASE + 26)
/*!
 * @messageid
 * @diagnosis
 * @recommendedaction
 *
 */

#define NFS_MSG_UNUSED_27               (GLFS_NFS_BASE + 27)
/*!
 * @messageid
 * @diagnosis
 * @recommendedaction
 *
 */

#define NFS_MSG_MKNOD_FAIL              (GLFS_NFS_BASE + 28)
/*!
 * @messageid
 * @diagnosis
 * @recommendedaction
 *
 */

#define NFS_MSG_OPT_INIT_FAIL           (GLFS_NFS_BASE + 29)
/*!
 * @messageid
 * @diagnosis
 * @recommendedaction
 *
 */


#define NFS_MSG_UNUSED_30               (GLFS_NFS_BASE + 30)
/*!
 * @messageid
 * @diagnosis
 * @recommendedaction
 *
 */

#define NFS_MSG_REMOVE_FAIL             (GLFS_NFS_BASE + 31)
/*!
 * @messageid
 * @diagnosis
 * @recommendedaction
 *
 */

#define NFS_MSG_RMDIR_CBK               (GLFS_NFS_BASE + 32)
/*!
 * @messageid
 * @diagnosis
 * @recommendedaction
 *
 */

#define NFS_MSG_UNUSED_33               (GLFS_NFS_BASE + 33)
/*!
 * @messageid
 * @diagnosis
 * @recommendedaction
 *
 */

#define NFS_MSG_RENAME_FAIL             (GLFS_NFS_BASE + 34)
/*!
 * @messageid
 * @diagnosis
 * @recommendedaction
 *
 */

#define NFS_MSG_UNUSED_35               (GLFS_NFS_BASE + 35)
/*!
 * @messageid
 * @diagnosis
 * @recommendedaction
 *
 */

#define NFS_MSG_LINK_FAIL               (GLFS_NFS_BASE + 36)
/*!
 * @messageid
 * @diagnosis
 * @recommendedaction
 *
 */

#define NFS_MSG_UNUSED_37               (GLFS_NFS_BASE + 37)
/*!
 * @messageid
 * @diagnosis
 * @recommendedaction
 *
 */

#define NFS_MSG_UNUSED_38               (GLFS_NFS_BASE + 38)
/*!
 * @messageid
 * @diagnosis
 * @recommendedaction
 *
 */

#define NFS_MSG_READDIR_FAIL            (GLFS_NFS_BASE + 39)
/*!
 * @messageid
 * @diagnosis
 * @recommendedaction
 *
 */

#define NFS_MSG_READDIRP_FAIL           (GLFS_NFS_BASE + 40)
/*!
 * @messageid
 * @diagnosis
 * @recommendedaction
 *
 */

#define NFS_MSG_UNUSED_41               (GLFS_NFS_BASE + 41)
/*!
 * @messageid
 * @diagnosis
 * @recommendedaction
 *
 */

#define NFS_MSG_UNUSED_42               (GLFS_NFS_BASE + 42)
/*!
 * @messageid
 * @diagnosis
 * @recommendedaction
 *
 */
#define NFS_MSG_FSTAT_FAIL              (GLFS_NFS_BASE + 43)
/*!
 * @messageid
 * @diagnosis
 * @recommendedaction
 *
 */

#define NFS_MSG_UNUSED_44               (GLFS_NFS_BASE + 44)
/*!
 * @messageid
 * @diagnosis
 * @recommendedaction
 *
 */

#define NFS_MSG_FSINFO_FAIL             (GLFS_NFS_BASE + 45)
/*!
 * @messageid
 * @diagnosis
 * @recommendedaction
 *
 */

#define NFS_MSG_UNUSED_46               (GLFS_NFS_BASE + 46)
/*!
 * @messageid
 * @diagnosis
 * @recommendedaction
 *
 */

#define NFS_MSG_PATHCONF_FAIL           (GLFS_NFS_BASE + 47)
/*!
 * @messageid
 * @diagnosis
 * @recommendedaction
 *
 */

#define NFS_MSG_UNUSED_48               (GLFS_NFS_BASE + 48)
/*!
 * @messageid
 * @diagnosis
 * @recommendedaction
 *
 */

#define NFS_MSG_COMMIT_FAIL              (GLFS_NFS_BASE + 49)
/*!
 * @messageid
 * @diagnosis
 * @recommendedaction
 *
 */

#define NFS_MSG_PROT_INIT_ADD_FAIL      (GLFS_NFS_BASE + 50)
/*!
 * @messageid
 * @diagnosis
 * @recommendedaction
 *
 */
#define NFS_MSG_FORMAT_FAIL             (GLFS_NFS_BASE + 51)
/*!
 * @messageid
 * @diagnosis
 * @recommendedaction
 *
 */

#define NFS_MSG_SNPRINTF_FAIL           (GLFS_NFS_BASE + 52)
/*!
 * @messageid
 * @diagnosis
 * @recommendedaction
 *
 */

#define NFS_MSG_VOLID_MISSING           (GLFS_NFS_BASE + 53)
/*!
 * @messageid
 * @diagnosis
 * @recommendedaction
 *
 */

#define NFS_MSG_PARSE_VOL_UUID_FAIL     (GLFS_NFS_BASE + 54)
/*!
 * @messageid
 * @diagnosis
 * @recommendedaction
 *
 */
#define NFS_MSG_STR2BOOL_FAIL            (GLFS_NFS_BASE + 55)
/*!
 * @messageid
 * @diagnosis
 * @recommendedaction
 *
 */

#define NFS_MSG_SUBVOL_INIT_FAIL        (GLFS_NFS_BASE + 56)
/*!
 * @messageid
 * @diagnosis
 * @recommendedaction
 *
 */

#define NFS_MSG_NO_MEMORY               (GLFS_NFS_BASE + 57)
/*!
 * @messageid
 * @diagnosis
 * @recommendedaction
 *
 */
#define NFS_MSG_LISTENERS_CREATE_FAIL   (GLFS_NFS_BASE + 58)
/*!
 * @messageid
 * @diagnosis
 * @recommendedaction
 *
 */

#define NFS_MSG_STATE_INIT_FAIL         (GLFS_NFS_BASE + 59)
/*!
 * @messageid
 * @diagnosis
 * @recommendedaction
 *
 */

#define NFS_MSG_RECONF_FAIL             (GLFS_NFS_BASE + 60)
/*!
 * @messageid
 * @diagnosis
 * @recommendedaction
 *
 */
#define NFS_MSG_RECONF_SUBVOL_FAIL      (GLFS_NFS_BASE + 61)
/*!
 * @messageid
 * @diagnosis
 * @recommendedaction
 *
 */

#define NFS_MSG_STR_TOO_LONG            (GLFS_NFS_BASE + 62)
/*!
 * @messageid
 * @diagnosis
 * @recommendedaction
 *
 */

#define NFS_MSG_STATE_MISSING           (GLFS_NFS_BASE + 63)
/*!
 * @messageid
 * @diagnosis
 * @recommendedaction
 *
 */

#define NFS_MSG_INDEX_NOT_FOUND         (GLFS_NFS_BASE + 64)
/*!
 * @messageid
 * @diagnosis
 * @recommendedaction
 *
 */

#define NFS_MSG_EXPORT_ID_FAIL          (GLFS_NFS_BASE + 65)
/*!
 * @messageid
 * @diagnosis
 * @recommendedaction
 *
 */

#define NFS_MSG_NO_RW_ACCESS            (GLFS_NFS_BASE + 66)
/*!
 * @messageid
 * @diagnosis
 * @recommendedaction
 *
 */
#define NFS_MSG_BAD_HANDLE              (GLFS_NFS_BASE + 67)
/*!
 * @messageid
 * @diagnosis
 * @recommendedaction
 *
 */
#define NFS_MSG_RESOLVE_FH_FAIL         (GLFS_NFS_BASE + 68)
/*!
 * @messageid
 * @diagnosis
 * @recommendedaction
 *
 */
#define NFS_MSG_RESOLVE_STAT            (GLFS_NFS_BASE + 69)
/*!
 * @messageid
 * @diagnosis
 * @recommendedaction
 *
 */
#define NFS_MSG_VOL_DISABLE             (GLFS_NFS_BASE + 70)
/*!
 * @messageid
 * @diagnosis
 * @recommendedaction
 *
 */

#define NFS_MSG_INIT_CALL_STAT_FAIL     (GLFS_NFS_BASE + 71)
/*!
 * @messageid
 * @diagnosis
 * @recommendedaction
 *
 */

#define NFS_MSG_ENCODE_FAIL             (GLFS_NFS_BASE + 72)
/*!
 * @messageid
 * @diagnosis
 * @recommendedaction
 *
 */

#define NFS_MSG_SERIALIZE_REPLY_FAIL    (GLFS_NFS_BASE + 73)
/*!
 * @messageid
 * @diagnosis
 * @recommendedaction
 *
 */

#define NFS_MSG_SUBMIT_REPLY_FAIL       (GLFS_NFS_BASE + 74)
/*!
 * @messageid
 * @diagnosis
 * @recommendedaction
 *
 */

#define NFS_MSG_UNUSED_75               (GLFS_NFS_BASE + 75)
/*!
 * @messageid
 * @diagnosis
 * @recommendedaction
 *
 */

#define NFS_MSG_UNUSED_76               (GLFS_NFS_BASE + 76)
/*!
 * @messageid
 * @diagnosis
 * @recommendedaction
 *
 */

#define NFS_MSG_STAT_FOP_FAIL           (GLFS_NFS_BASE + 77)
/*!
 * @messageid
 * @diagnosis
 * @recommendedaction
 *
 */

#define NFS_MSG_GETATTR_FAIL            (GLFS_NFS_BASE + 78)
/*!
 * @messageid
 * @diagnosis
 * @recommendedaction
 *
 */

#define NFS_MSG_UNUSED_79               (GLFS_NFS_BASE + 79)
/*!
 * @messageid
 * @diagnosis
 * @recommendedaction
 *
 */

#define NFS_MSG_UNUSED_80               (GLFS_NFS_BASE + 80)
/*!
 * @messageid
 * @diagnosis
 * @recommendedaction
 *
 */

#define NFS_MSG_TIMESTAMP_NO_SYNC       (GLFS_NFS_BASE + 81)
/*!
 * @messageid
 * @diagnosis
 * @recommendedaction
 *
 */

#define NFS_MSG_SETATTR_INVALID         (GLFS_NFS_BASE + 82)
/*!
 * @messageid
 * @diagnosis
 * @recommendedaction
 *
 */

#define NFS_MSG_SETATTR_FAIL            (GLFS_NFS_BASE + 83)
/*!
 * @messageid
 * @diagnosis
 * @recommendedaction
 *
 */

#define NFS_MSG_UNUSED_84               (GLFS_NFS_BASE + 84)
/*!
 * @messageid
 * @diagnosis
 * @recommendedaction
 *
 */

#define NFS_MSG_ACCESS_PROC_FAIL        (GLFS_NFS_BASE + 85)
/*!
 * @messageid
 * @diagnosis
 * @recommendedaction
 *
 */

#define NFS_MSG_PGM_NOT_FOUND           (GLFS_NFS_BASE + 86)
/*!
 * @messageid
 * @diagnosis
 * @recommendedaction
 *
 */

#define NFS_MSG_PGM_INIT_FAIL           (GLFS_NFS_BASE + 87)
/*!
 * @messageid
 * @diagnosis
 * @recommendedaction
 *
 */

#define NFS_MSG_PGM_REG_FAIL            (GLFS_NFS_BASE + 88)
/*!
 * @messageid
 * @diagnosis
 * @recommendedaction
 *
 */

#define NFS_MSG_LOOKUP_ROOT_FAIL        (GLFS_NFS_BASE + 89)
/*!
 * @messageid
 * @diagnosis
 * @recommendedaction
 *
 */

#define NFS_MSG_ROOT_LOC_INIT_FAIL      (GLFS_NFS_BASE + 90)
/*!
 * @messageid
 * @diagnosis
 * @recommendedaction
 *
 */

#define NFS_MSG_STARTUP_FAIL            (GLFS_NFS_BASE + 91)
/*!
 * @messageid
 * @diagnosis
 * @recommendedaction
 *
 */

#define NFS_MSG_XLATOR_INIT_FAIL        (GLFS_NFS_BASE + 92)
/*!
 * @messageid
 * @diagnosis
 * @recommendedaction
 *
 */

#define NFS_MSG_NFS_MAN_DISABLE         (GLFS_NFS_BASE + 93)
/*!
 * @messageid
 * @diagnosis
 * @recommendedaction
 *
 */

#define NFS_MSG_DICT_GET_FAILED         (GLFS_NFS_BASE + 94)
/*!
 * @messageid
 * @diagnosis
 * @recommendedaction
 *
 */

#define NFS_MSG_PARSE_FAIL              (GLFS_NFS_BASE + 95)
/*!
 * @messageid
 * @diagnosis
 * @recommendedaction
 *
 */

#define NFS_MSG_NLM_MAN_DISABLE         (GLFS_NFS_BASE + 96)
/*!
 * @messageid
 * @diagnosis
 * @recommendedaction
 *
 */

#define NFS_MSG_ACL_MAN_DISABLE         (GLFS_NFS_BASE + 97)
/*!
 * @messageid
 * @diagnosis
 * @recommendedaction
 *
 */

#define NFS_MSG_DICT_SET_FAILED         (GLFS_NFS_BASE + 98)
/*!
 * @messageid
 * @diagnosis
 * @recommendedaction
 *
 */

#define NFS_MSG_INIT_GRP_CACHE_FAIL     (GLFS_NFS_BASE + 99)
/*!
 * @messageid
 * @diagnosis
 * @recommendedaction
 *
 */

#define NFS_MSG_NO_PERM                 (GLFS_NFS_BASE + 100)
/*!
 * @messageid
 * @diagnosis
 * @recommendedaction
 *
 */

#define NFS_MSG_REG_FILE_ERROR          (GLFS_NFS_BASE + 101)
/*!
 * @messageid
 * @diagnosis
 * @recommendedaction
 *
 */

#define NFS_MSG_RPC_INIT_FAIL           (GLFS_NFS_BASE + 102)
/*!
 * @messageid
 * @diagnosis
 * @recommendedaction
 *
 */

#define NFS_MSG_RPC_CONFIG_FAIL         (GLFS_NFS_BASE + 103)
/*!
 * @messageid
 * @diagnosis
 * @recommendedaction
 *
 */

#define NFS_MSG_RECONFIG_PATH           (GLFS_NFS_BASE + 104)
/*!
 * @messageid
 * @diagnosis
 * @recommendedaction
 *
 */

#define NFS_MSG_RECONFIG_VALUE          (GLFS_NFS_BASE + 105)
/*!
 * @messageid
 * @diagnosis
 * @recommendedaction
 *
 */

#define NFS_MSG_RECONFIG_VOL            (GLFS_NFS_BASE + 106)
/*!
 * @messageid
 * @diagnosis
 * @recommendedaction
 *
 */

#define NFS_MSG_NLM_INFO                (GLFS_NFS_BASE + 107)
/*!
 * @messageid
 * @diagnosis
 * @recommendedaction
 *
 */

#define NFS_MSG_ACL_INFO                (GLFS_NFS_BASE + 108)
/*!
 * @messageid
 * @diagnosis
 * @recommendedaction
 *
 */

#define NFS_MSG_INIT_FAIL               (GLFS_NFS_BASE + 109)
/*!
 * @messageid
 * @diagnosis
 * @recommendedaction
 *
 */

#define NFS_MSG_STARTED                 (GLFS_NFS_BASE + 110)
/*!
 * @messageid
 * @diagnosis
 * @recommendedaction
 *
 */

#define NFS_MSG_VOL_NOT_FOUND           (GLFS_NFS_BASE + 111)
/*!
 * @messageid
 * @diagnosis
 * @recommendedaction
 *
 */

#define NFS_MSG_RECONFIG_ENABLE         (GLFS_NFS_BASE + 112)
/*!
 * @messageid
 * @diagnosis
 * @recommendedaction
 *
 */

#define NFS_MSG_RECONFIG_FAIL           (GLFS_NFS_BASE + 113)
/*!
 * @messageid
 * @diagnosis
 * @recommendedaction
 *
 */

#define NFS_MSG_MNT_STATE_NOT_FOUND     (GLFS_NFS_BASE + 114)
/*!
 * @messageid
 * @diagnosis
 * @recommendedaction
 *
 */

#define NFS_MSG_ENCODE_MSG_FAIL         (GLFS_NFS_BASE + 115)
/*!
 * @messageid
 * @diagnosis
 * @recommendedaction
 *
 */

#define NFS_MSG_REP_SUBMIT_FAIL         (GLFS_NFS_BASE + 116)
/*!
 * @messageid
 * @diagnosis
 * @recommendedaction
 *
 */

#define NFS_MSG_READ_LOCKED             (GLFS_NFS_BASE + 117)
/*!
 * @messageid
 * @diagnosis
 * @recommendedaction
 *
 */

#define NFS_MSG_MODIFY_LOCKED           (GLFS_NFS_BASE + 118)
/*!
 * @messageid
 * @diagnosis
 * @recommendedaction
 *
 */

#define NFS_MSG_RWTAB_OVERWRITE_FAIL    (GLFS_NFS_BASE + 119)
/*!
 * @messageid
 * @diagnosis
 * @recommendedaction
 *
 */

#define NFS_MSG_UPDATE_FAIL             (GLFS_NFS_BASE + 120)
/*!
 * @messageid
 * @diagnosis
 * @recommendedaction
 *
 */

#define NFS_MSG_OPEN_FAIL               (GLFS_NFS_BASE + 121)
/*!
 * @messageid
 * @diagnosis
 * @recommendedaction
 *
 */

#define NFS_MSG_LOCK_FAIL               (GLFS_NFS_BASE + 122)
/*!
 * @messageid
 * @diagnosis
 * @recommendedaction
 *
 */

#define NFS_MSG_REWRITE_ERROR           (GLFS_NFS_BASE + 123)
/*!
 * @messageid
 * @diagnosis
 * @recommendedaction
 *
 */

#define NFS_MSG_HASH_PATH_FAIL          (GLFS_NFS_BASE + 124)
/*!
 * @messageid
 * @diagnosis
 * @recommendedaction
 *
 */

#define NFS_MSG_LOOKUP_MNT_ERROR        (GLFS_NFS_BASE + 125)
/*!
 * @messageid
 * @diagnosis
 * @recommendedaction
 *
 */

#define NFS_MSG_GET_ROOT_INODE_FAIL     (GLFS_NFS_BASE + 126)
/*!
 * @messageid
 * @diagnosis
 * @recommendedaction
 *
 */

#define NFS_MSG_RESOLVE_INODE_FAIL      (GLFS_NFS_BASE + 127)
/*!
 * @messageid
 * @diagnosis
 * @recommendedaction
 *
 */

#define NFS_MSG_RESOLVE_SUBDIR_FAIL     (GLFS_NFS_BASE + 128)
/*!
 * @messageid
 * @diagnosis
 * @recommendedaction
 *
 */

#define NFS_MSG_RESOLVE_SYMLINK_ERROR   (GLFS_NFS_BASE + 129)
/*!
 * @messageid
 * @diagnosis
 * @recommendedaction
 *
 */

#define NFS_MSG_RESOLVE_ERROR           (GLFS_NFS_BASE + 130)
/*!
 * @messageid
 * @diagnosis
 * @recommendedaction
 *
 */

#define NFS_MSG_UNSUPPORTED_VERSION     (GLFS_NFS_BASE + 131)
/*!
 * @messageid
 * @diagnosis
 * @recommendedaction
 *
 */

#define NFS_MSG_AUTH_VERIFY_FAILED      (GLFS_NFS_BASE + 132)
/*!
 * @messageid
 * @diagnosis
 * @recommendedaction
 *
 */

#define NFS_MSG_PEER_NOT_ALLOWED        (GLFS_NFS_BASE + 133)
/*!
 * @messageid
 * @diagnosis
 * @recommendedaction
 *
 */


#define NFS_MSG_GET_PEER_ADDR_FAIL      (GLFS_NFS_BASE + 134)
/*!
 * @messageid
 * @diagnosis
 * @recommendedaction
 *
 */

#define NFS_MSG_BAD_PEER                (GLFS_NFS_BASE + 135)
/*!
 * @messageid
 * @diagnosis
 * @recommendedaction
 *
 */

#define NFS_MSG_PEER_TOO_LONG            (GLFS_NFS_BASE + 136)
/*!
 * @messageid
 * @diagnosis
 * @recommendedaction
 *
 */

#define NFS_MSG_CALLER_NOT_FOUND        (GLFS_NFS_BASE + 137)
/*!
 * @messageid
 * @diagnosis
 * @recommendedaction
 *
 */

#define NFS_MSG_GET_REMOTE_NAME_FAIL    (GLFS_NFS_BASE + 138)
/*!
 * @messageid
 * @diagnosis
 * @recommendedaction
 *
 */

#define NFS_MSG_UNKNOWN_MNT_TYPE        (GLFS_NFS_BASE + 139)
/*!
 * @messageid
 * @diagnosis
 * @recommendedaction
 *
 */

#define NFS_MSG_PARSE_HOSTSPEC_FAIL     (GLFS_NFS_BASE + 140)
/*!
 * @messageid
 * @diagnosis
 * @recommendedaction
 *
 */

#define NFS_MSG_PARSE_AUTH_PARAM_FAIL   (GLFS_NFS_BASE + 141)
/*!
 * @messageid
 * @diagnosis
 * @recommendedaction
 *
 */

#define NFS_MSG_SET_EXP_FAIL            (GLFS_NFS_BASE + 142)
/*!
 * @messageid
 * @diagnosis
 * @recommendedaction
 *
 */

#define NFS_MSG_INIT_DIR_EXP_FAIL       (GLFS_NFS_BASE + 143)
/*!
 * @messageid
 * @diagnosis
 * @recommendedaction
 *
 */

#define NFS_MSG_DIR_EXP_SETUP_FAIL      (GLFS_NFS_BASE + 144)
/*!
 * @messageid
 * @diagnosis
 * @recommendedaction
 *
 */

#define NFS_MSG_VOL_INIT_FAIL           (GLFS_NFS_BASE + 145)
/*!
 * @messageid
 * @diagnosis
 * @recommendedaction
 *
 */

#define NFS_MSG_AUTH_ERROR              (GLFS_NFS_BASE + 146)
/*!
 * @messageid
 * @diagnosis
 * @recommendedaction
 *
 */

#define NFS_MSG_UPDATING_EXP            (GLFS_NFS_BASE + 147)
/*!
 * @messageid
 * @diagnosis
 * @recommendedaction
 *
 */

#define NFS_MSG_SET_EXP_AUTH_PARAM_FAIL (GLFS_NFS_BASE + 148)
/*!
 * @messageid
 * @diagnosis
 * @recommendedaction
 *
 */

#define NFS_MSG_UPDATING_NET_GRP        (GLFS_NFS_BASE + 149)
/*!
 * @messageid
 * @diagnosis
 * @recommendedaction
 *
 */

#define NFS_MSG_SET_NET_GRP_FAIL        (GLFS_NFS_BASE + 150)
/*!
 * @messageid
 * @diagnosis
 * @recommendedaction
 *
 */

#define NFS_MSG_PURGING_AUTH_CACHE      (GLFS_NFS_BASE + 151)
/*!
 * @messageid
 * @diagnosis
 * @recommendedaction
 *
 */

#define NFS_MSG_MNT_STATE_INIT_FAIL     (GLFS_NFS_BASE + 152)
/*!
 * @messageid
 * @diagnosis
 * @recommendedaction
 *
 */

#define NFS_MSG_EXP_AUTH_DISABLED       (GLFS_NFS_BASE + 153)
/*!
 * @messageid
 * @diagnosis
 * @recommendedaction
 *
 */

#define NFS_MSG_FH_TO_VOL_FAIL          (GLFS_NFS_BASE + 154)
/*!
 * @messageid
 * @diagnosis
 * @recommendedaction
 *
 */

#define NFS_MSG_INODE_SHARES_NOT_FOUND  (GLFS_NFS_BASE + 155)
/*!
 * @messageid
 * @diagnosis
 * @recommendedaction
 *
 */

#define NFS_MSG_VOLUME_ERROR            (GLFS_NFS_BASE + 156)
/*!
 * @messageid
 * @diagnosis
 * @recommendedaction
 *
 */

#define NFS_MSG_GET_USER_ACL_FAIL       (GLFS_NFS_BASE + 157)
/*!
 * @messageid
 * @diagnosis
 * @recommendedaction
 *
 */

#define NFS_MSG_GET_DEF_ACL_FAIL        (GLFS_NFS_BASE + 158)
/*!
 * @messageid
 * @diagnosis
 * @recommendedaction
 *
 */

#define NFS_MSG_SET_USER_ACL_FAIL       (GLFS_NFS_BASE + 159)
/*!
 * @messageid
 * @diagnosis
 * @recommendedaction
 *
 */

#define NFS_MSG_SET_DEF_ACL_FAIL        (GLFS_NFS_BASE + 160)
/*!
 * @messageid
 * @diagnosis
 * @recommendedaction
 *
 */

#define NFS_MSG_ACL_INIT_FAIL           (GLFS_NFS_BASE + 161)
/*!
 * @messageid
 * @diagnosis
 * @recommendedaction
 *
 */

#define NFS_MSG_LOAD_PARSE_ERROR        (GLFS_NFS_BASE + 162)
/*!
 * @messageid
 * @diagnosis
 * @recommendedaction
 *
 */

#define NFS_MSG_CLNT_CALL_ERROR         (GLFS_NFS_BASE + 163)
/*!
 * @messageid
 * @diagnosis
 * @recommendedaction
 *
 */

#define NFS_MSG_CLNT_CREATE_ERROR       (GLFS_NFS_BASE + 164)
/*!
 * @messageid
 * @diagnosis
 * @recommendedaction
 *
 */

#define NFS_MSG_NLM_GRACE_PERIOD        (GLFS_NFS_BASE + 165)
/*!
 * @messageid
 * @diagnosis
 * @recommendedaction
 *
 */

#define NFS_MSG_RPC_CLNT_ERROR          (GLFS_NFS_BASE + 166)
/*!
 * @messageid
 * @diagnosis
 * @recommendedaction
 *
 */

#define NFS_MSG_GET_PORT_ERROR          (GLFS_NFS_BASE + 167)
/*!
 * @messageid
 * @diagnosis
 * @recommendedaction
 *
 */

#define NFS_MSG_NLMCLNT_NOT_FOUND       (GLFS_NFS_BASE + 168)
/*!
 * @messageid
 * @diagnosis
 * @recommendedaction
 *
 */

#define NFS_MSG_FD_LOOKUP_NULL          (GLFS_NFS_BASE + 169)
/*!
 * @messageid
 * @diagnosis
 * @recommendedaction
 *
 */

#define NFS_MSG_SM_NOTIFY               (GLFS_NFS_BASE + 170)
/*!
 * @messageid
 * @diagnosis
 * @recommendedaction
 *
 */

#define NFS_MSG_NLM_INIT_FAIL           (GLFS_NFS_BASE + 171)
/*!
 * @messageid
 * @diagnosis
 * @recommendedaction
 *
 */

#define NFS_MSG_START_ERROR             (GLFS_NFS_BASE + 172)
/*!
 * @messageid
 * @diagnosis
 * @recommendedaction
 *
 */

#define NFS_MSG_UNLINK_ERROR            (GLFS_NFS_BASE + 173)
/*!
 * @messageid
 * @diagnosis
 * @recommendedaction
 *
 */

#define NFS_MSG_SHARE_LIST_STORE_FAIL   (GLFS_NFS_BASE + 174)
/*!
 * @messageid
 * @diagnosis
 * @recommendedaction
 *
 */

#define NFS_MSG_CLIENT_NOT_FOUND        (GLFS_NFS_BASE + 175)
/*!
 * @messageid
 * @diagnosis
 * @recommendedaction
 *
 */

#define NFS_MSG_SHARE_CALL_FAIL         (GLFS_NFS_BASE + 176)
/*!
 * @messageid
 * @diagnosis
 * @recommendedaction
 *
 */

#define NFS_MSG_UNSHARE_CALL_FAIL       (GLFS_NFS_BASE + 177)
/*!
 * @messageid
 * @diagnosis
 * @recommendedaction
 *
 */

#define NFS_MSG_GET_PID_FAIL            (GLFS_NFS_BASE + 178)
/*!
 * @messageid
 * @diagnosis
 * @recommendedaction
 *
 */

#define NFS_MSG_ARG_FREE_FAIL           (GLFS_NFS_BASE + 179)
/*!
 * @messageid
 * @diagnosis
 * @recommendedaction
 *
 */

#define NFS_MSG_PMAP_UNSET_FAIL         (GLFS_NFS_BASE + 180)
/*!
 * @messageid
 * @diagnosis
 * @recommendedaction
 *
 */

#define NFS_MSG_UDP_SERV_FAIL           (GLFS_NFS_BASE + 181)
/*!
 * @messageid
 * @diagnosis
 * @recommendedaction
 *
 */

#define NFS_MSG_REG_NLMCBK_FAIL         (GLFS_NFS_BASE + 182)
/*!
 * @messageid
 * @diagnosis
 * @recommendedaction
 *
 */

#define NFS_MSG_TCP_SERV_FAIL           (GLFS_NFS_BASE + 183)
/*!
 * @messageid
 * @diagnosis
 * @recommendedaction
 *
 */

#define NFS_MSG_SVC_RUN_RETURNED        (GLFS_NFS_BASE + 184)
/*!
 * @messageid
 * @diagnosis
 * @recommendedaction
 *
 */

#define NFS_MSG_XLATOR_SET_FAIL         (GLFS_NFS_BASE + 185)
/*!
 * @messageid
 * @diagnosis
 * @recommendedaction
 *
 */

#define NFS_MSG_SVC_ERROR               (GLFS_NFS_BASE + 186)
/*!
 * @messageid
 * @diagnosis
 * @recommendedaction
 *
 */

#define NFS_MSG_GET_FH_FAIL             (GLFS_NFS_BASE + 187)
/*!
 * @messageid
 * @diagnosis
 * @recommendedaction
 *
 */

#define NFS_MSG_FIND_FIRST_MATCH_FAIL   (GLFS_NFS_BASE + 188)
/*!
 * @messageid
 * @diagnosis
 * @recommendedaction
 *
 */

#define NFS_MSG_NETGRP_NOT_FOUND        (GLFS_NFS_BASE + 189)
/*!
 * @messageid
 * @diagnosis
 * @recommendedaction
 *
 */

#define NFS_MSG_FILE_OP_FAILED          (GLFS_NFS_BASE + 190)
/*!
 * @messageid
 * @diagnosis
 * @recommendedaction
 *
 */

#define NFS_MSG_PATH_RESOLVE_FAIL       (GLFS_NFS_BASE + 191)
/*!
 * @messageid
 * @diagnosis
 * @recommendedaction
 *
 */

#define NFS_MSG_LOC_FILL_RESOLVE_FAIL   (GLFS_NFS_BASE + 192)
/*!
 * @messageid
 * @diagnosis
 * @recommendedaction
 *
 */

#define NFS_MSG_INODE_NOT_FOUND         (GLFS_NFS_BASE + 193)
/*!
 * @messageid
 * @diagnosis
 * @recommendedaction
 *
 */

#define NFS_MSG_INODE_CTX_STORE_FAIL    (GLFS_NFS_BASE + 194)
/*!
 * @messageid
 * @diagnosis
 * @recommendedaction
 *
 */

#define NFS_MSG_GETPWUID_FAIL           (GLFS_NFS_BASE + 195)
/*!
 * @messageid
 * @diagnosis
 * @recommendedaction
 *
 */

#define NFS_MSG_MAP_GRP_LIST_FAIL       (GLFS_NFS_BASE + 196)
/*!
 * @messageid
 * @diagnosis
 * @recommendedaction
 *
 */

#define NFS_MSG_PARSE_DIR_FAIL          (GLFS_NFS_BASE + 197)
/*!
 * @messageid
 * @diagnosis
 * @recommendedaction
 *
 */

#define NFS_MSG_LOOKUP_FAIL             (GLFS_NFS_BASE + 198)
/*!
 * @messageid
 * @diagnosis
 * @recommendedaction
 *
 */

#define NFS_MSG_STAT_ERROR              (GLFS_NFS_BASE + 199)
/*!
 * @messageid
 * @diagnosis
 * @recommendedaction
 *
 */

#define NFS_MSG_GFID_DICT_CREATE_FAIL   (GLFS_NFS_BASE + 200)
/*!
 * @messageid
 * @diagnosis
 * @recommendedaction
 *
 */

#define NFS_MSG_HASH_XLATOR_FAIL        (GLFS_NFS_BASE + 201)
/*!
 * @messageid
 * @diagnosis
 * @recommendedaction
 *
 */

#define NFS_MSG_ENABLE_THROTTLE_FAIL    (GLFS_NFS_BASE + 202)
/*!
 * @messageid
 * @diagnosis
 * @recommendedaction
 *
 */


/*------------*/
#define glfs_msg_end_x GLFS_MSGID_END, "Invalid: End of messages"


#endif /* _NFS_MESSAGES_H_ */




