/*
  Copyright (c) 2014 Red Hat, Inc. <http://www.redhat.com>
  This file is part of GlusterFS.

  This file is licensed to you under your choice of the GNU Lesser
  General Public License, version 3 or any later version (LGPLv3 or
  later), or the GNU General Public License, version 2 (GPLv2), in all
  cases as published by the Free Software Foundation.
*/

#ifndef _POSIX_MESSAGES_H_
#define _POSIX_MESSAGES_H_

#ifndef _CONFIG_H
#define _CONFIG_H
#include "config.h"
#endif

#include "glfs-message-id.h"

/*! \file posix-messages.h
 *  \brief Psix log-message IDs and their descriptions
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

#define POSIX_COMP_BASE         GLFS_MSGID_COMP_POSIX
#define GLFS_NUM_MESSAGES       110
#define GLFS_MSGID_END          (POSIX_COMP_BASE + GLFS_NUM_MESSAGES + 1)
/* Messaged with message IDs */
#define glfs_msg_start_x POSIX_COMP_BASE, "Invalid: Start of messages"
/*------------*/

/*!
 * @messageid 106001
 * @diagnosis Operation could not be performed because the server quorum was not
 *            met
 * @recommendedaction Ensure that other peer nodes are online and reachable from
 *                    the local peer node
 */

#define P_MSG_XATTR_FAILED                      (POSIX_COMP_BASE + 1)
/*!
 * @messageid
 * @diagnosis
 * @recommendedaction
 *
 */

#define P_MSG_NULL_GFID                         (POSIX_COMP_BASE + 2)
/*!
 * @messageid
 * @diagnosis
 * @recommendedaction
 *
 */


#define P_MSG_FCNTL_FAILED                      (POSIX_COMP_BASE + 3)
/*!
 * @messageid
 * @diagnosis
 * @recommendedaction
 *
 */

#define P_MSG_READV_FAILED                      (POSIX_COMP_BASE + 4)
/*!
 * @messageid
 * @diagnosis
 * @recommendedaction
 *
 */

#define P_MSG_FSTAT_FAILED                      (POSIX_COMP_BASE + 5)
/*!
 * @messageid
 * @diagnosis
 * @recommendedaction
 *
 */

#define P_MSG_PFD_NULL                          (POSIX_COMP_BASE + 6)
/*!
 * @messageid
 * @diagnosis
 * @recommendedaction
 *
 */

#define P_MSG_INVALID_ARGUMENT                  (POSIX_COMP_BASE + 7)
/*!
 * @messageid
 * @diagnosis
 * @recommendedaction
 *
 */

#define P_MSG_IO_SUBMIT_FAILED                  (POSIX_COMP_BASE + 8)
/*!
 * @messageid
 * @diagnosis
 * @recommendedaction
 *
 */

#define P_MSG_WRITEV_FAILED                     (POSIX_COMP_BASE + 9)
/*!
 * @messageid
 * @diagnosis
 * @recommendedaction
 *
 */

#define P_MSG_IO_GETEVENTS_FAILED               (POSIX_COMP_BASE + 10)
/*!
 * @messageid
 * @diagnosis
 * @recommendedaction
 *
 */

#define P_MSG_UNKNOWN_OP                        (POSIX_COMP_BASE + 11)
/*!
 * @messageid
 * @diagnosis
 * @recommendedaction
 *
 */

#define P_MSG_AIO_UNAVAILABLE                   (POSIX_COMP_BASE + 12)
/*!
 * @messageid
 * @diagnosis
 * @recommendedaction
 *
 */

#define P_MSG_IO_SETUP_FAILED                   (POSIX_COMP_BASE + 13)
/*!
 * @messageid
 * @diagnosis
 * @recommendedaction
 *
 */

#define P_MSG_ZEROFILL_FAILED                   (POSIX_COMP_BASE + 14)
/*!
 * @messageid
 * @diagnosis
 * @recommendedaction
 *
 */

#define P_MSG_OPENDIR_FAILED                    (POSIX_COMP_BASE + 15)
/*!
 * @messageid
 * @diagnosis
 * @recommendedaction
 *
 */

#define P_MSG_DIRFD_FAILED                      (POSIX_COMP_BASE + 16)
/*!
 * @messageid
 * @diagnosis
 * @recommendedaction
 *
 */

#define P_MSG_FD_PATH_SETTING_FAILED            (POSIX_COMP_BASE + 17)
/*!
 * @messageid
 * @diagnosis
 * @recommendedaction
 *
 */

#define P_MSG_LSTAT_FAILED                      (POSIX_COMP_BASE + 18)
/*!
 * @messageid
 * @diagnosis
 * @recommendedaction
 *
 */

#define P_MSG_READYLINK_FAILED                  (POSIX_COMP_BASE + 19)
/*!
 * @messageid
 * @diagnosis
 * @recommendedaction
 *
 */

#define P_MSG_GFID_FAILED                       (POSIX_COMP_BASE + 20)
/*!
 * @messageid
 * @diagnosis
 * @recommendedaction
 *
 */

#define P_MSG_CREATE_FAILED                     (POSIX_COMP_BASE + 21)
/*!
 * @messageid
 * @diagnosis
 * @recommendedaction
 *
 */

#define P_MSG_MKNOD_FAILED                      (POSIX_COMP_BASE + 22)
/*!
 * @messageid
 * @diagnosis
 * @recommendedaction
 *
 */

#define P_MSG_LCHOWN_FAILED                     (POSIX_COMP_BASE + 23)
/*!
 * @messageid
 * @diagnosis
 * @recommendedaction
 *
 */

#define P_MSG_ACL_FAILED                        (POSIX_COMP_BASE + 24)
/*!
 * @messageid
 * @diagnosis
 * @recommendedaction
 *
 */

#define P_MSG_MKDIR_NOT_PERMITTED               (POSIX_COMP_BASE + 25)
/*!
 * @messageid
 * @diagnosis
 * @recommendedaction
 *
 */

#define P_MSG_DIR_OF_SAME_ID                    (POSIX_COMP_BASE + 26)
/*!
 * @messageid
 * @diagnosis
 * @recommendedaction
 *
 */

#define P_MSG_MKDIR_FAILED                      (POSIX_COMP_BASE + 27)
/*!
 * @messageid
 * @diagnosis
 * @recommendedaction
 *
 */

#define P_MSG_CHOWN_FAILED                      (POSIX_COMP_BASE + 28)
/*!
 * @messageid
 * @diagnosis
 * @recommendedaction
 *
 */

#define P_MSG_UNLINK_FAILED                     (POSIX_COMP_BASE + 29)
/*!
 * @messageid
 * @diagnosis
 * @recommendedaction
 *
 */

#define P_MSG_KEY_STATUS_INFO                   (POSIX_COMP_BASE + 30)
/*!
 * @messageid
 * @diagnosis
 * @recommendedaction
 *
 */

#define P_MSG_XATTR_STATUS                      (POSIX_COMP_BASE + 31)
/*!
 * @messageid
 * @diagnosis
 * @recommendedaction
 *
 */

#define P_MSG_RMDIR_NOT_PERMITTED               (POSIX_COMP_BASE + 32)
/*!
 * @messageid
 * @diagnosis
 * @recommendedaction
 *
 */

#define P_MSG_RMDIR_FAILED                      (POSIX_COMP_BASE + 33)
/*!
 * @messageid
 * @diagnosis
 * @recommendedaction
 *
 */

#define P_MSG_DIR_OPERATION_FAILED              (POSIX_COMP_BASE + 34)
/*!
 * @messageid
 * @diagnosis
 * @recommendedaction
 *
 */

#define P_MSG_SYMLINK_FAILED                    (POSIX_COMP_BASE + 35)
/*!
 * @messageid
 * @diagnosis
 * @recommendedaction
 *
 */

#define P_MSG_DIR_FOUND                         (POSIX_COMP_BASE + 36)
/*!
 * @messageid
 * @diagnosis
 * @recommendedaction
 *
 */

#define P_MSG_LINK_FAILED                       (POSIX_COMP_BASE + 37)
/*!
 * @messageid
 * @diagnosis
 * @recommendedaction
 *
 */

#define P_MSG_TRUNCATE_FAILED                   (POSIX_COMP_BASE + 38)
/*!
 * @messageid
 * @diagnosis
 * @recommendedaction
 *
 */

#define P_MSG_FILE_OP_FAILED                    (POSIX_COMP_BASE + 39)
/*!
 * @messageid
 * @diagnosis
 * @recommendedaction
 *
 */

#define P_MSG_READ_FAILED                       (POSIX_COMP_BASE + 40)
/*!
 * @messageid
 * @diagnosis
 * @recommendedaction
 *
 */

#define P_MSG_DICT_SET_FAILED                   (POSIX_COMP_BASE + 41)
/*!
 * @messageid
 * @diagnosis
 * @recommendedaction
 *
 */

#define P_MSG_STATVFS_FAILED                    (POSIX_COMP_BASE + 42)
/*!
 * @messageid
 * @diagnosis
 * @recommendedaction
 *
 */

#define P_MSG_DIR_NOT_NULL                      (POSIX_COMP_BASE + 43)
/*!
 * @messageid
 * @diagnosis
 * @recommendedaction
 *
 */

#define P_MSG_FSYNC_FAILED                      (POSIX_COMP_BASE + 44)
/*!
 * @messageid
 * @diagnosis
 * @recommendedaction
 *
 */

#define P_MSG_CLOSE_FAILED                      (POSIX_COMP_BASE + 45)
/*!
 * @messageid
 * @diagnosis
 * @recommendedaction
 *
 */

#define P_MSG_GETTING_FILENAME_FAILED           (POSIX_COMP_BASE + 46)
/*!
 * @messageid
 * @diagnosis
 * @recommendedaction
 *
 */

#define P_MSG_INODE_PATH_GET_FAILED             (POSIX_COMP_BASE + 47)
/*!
 * @messageid
 * @diagnosis
 * @recommendedaction
 *
 */

#define P_MSG_GET_KEY_VALUE_FAILED              (POSIX_COMP_BASE + 48)
/*!
 * @messageid
 * @diagnosis
 * @recommendedaction
 *
 */

#define P_MSG_CHMOD_FAILED                      (POSIX_COMP_BASE + 49)
/*!
 * @messageid
 * @diagnosis
 * @recommendedaction
 *
 */

#define P_MSG_FCHMOD_FAILED                     (POSIX_COMP_BASE + 50)
/*!
 * @messageid
 * @diagnosis
 * @recommendedaction
 *
 */

#define P_MSG_FCHOWN_FAILED                     (POSIX_COMP_BASE + 51)
/*!
 * @messageid
 * @diagnosis
 * @recommendedaction
 *
 */

#define P_MSG_UTIMES_FAILED                     (POSIX_COMP_BASE + 52)
/*!
 * @messageid
 * @diagnosis
 * @recommendedaction
 *
 */

#define P_MSG_FUTIMES_FAILED                    (POSIX_COMP_BASE + 53)
/*!
 * @messageid
 * @diagnosis
 * @recommendedaction
 *
 */

#define P_MSG_XATTR_NOT_REMOVED                 (POSIX_COMP_BASE + 54)
/*!
 * @messageid
 * @diagnosis
 * @recommendedaction
 *
 */

#define P_MSG_PFD_GET_FAILED                    (POSIX_COMP_BASE + 55)
/*!
 * @messageid
 * @diagnosis
 * @recommendedaction
 *
 */

#define P_MSG_ACCESS_FAILED                     (POSIX_COMP_BASE + 56)
/*!
 * @messageid
 * @diagnosis
 * @recommendedaction
 *
 */

#define P_MSG_PREAD_FAILED                      (POSIX_COMP_BASE + 57)
/*!
 * @messageid
 * @diagnosis
 * @recommendedaction
 *
 */

#define P_MSG_UUID_NULL                         (POSIX_COMP_BASE + 58)
/*!
 * @messageid
 * @diagnosis
 * @recommendedaction
 *
 */

#define P_MSG_EXPORT_DIR_MISSING                (POSIX_COMP_BASE + 59)
/*!
 * @messageid
 * @diagnosis
 * @recommendedaction
 *
 */

#define P_MSG_SUBVOLUME_ERROR                   (POSIX_COMP_BASE + 60)
/*!
 * @messageid
 * @diagnosis
 * @recommendedaction
 *
 */

#define P_MSG_VOLUME_DANGLING                   (POSIX_COMP_BASE + 61)
/*!
 * @messageid
 * @diagnosis
 * @recommendedaction
 *
 */

#define P_MSG_INVALID_OPTION                    (POSIX_COMP_BASE + 62)
/*!
 * @messageid
 * @diagnosis
 * @recommendedaction
 *
 */

#define P_MSG_INVALID_VOLUME_ID                 (POSIX_COMP_BASE + 63)
/*!
 * @messageid
 * @diagnosis
 * @recommendedaction
 *
 */

#define P_MSG_VOLUME_ID_ABSENT                  (POSIX_COMP_BASE + 64)
/*!
 * @messageid
 * @diagnosis
 * @recommendedaction
 *
 */

#define P_MSG_HOSTNAME_MISSING                  (POSIX_COMP_BASE + 65)
/*!
 * @messageid
 * @diagnosis
 * @recommendedaction
 *
 */

#define P_MSG_SET_ULIMIT_FAILED                 (POSIX_COMP_BASE + 66)
/*!
 * @messageid
 * @diagnosis
 * @recommendedaction
 *
 */

#define P_MSG_SET_FILE_MAX_FAILED               (POSIX_COMP_BASE + 67)
/*!
 * @messageid
 * @diagnosis
 * @recommendedaction
 *
 */

#define P_MSG_MAX_FILE_OPEN                     (POSIX_COMP_BASE + 68)
/*!
 * @messageid
 * @diagnosis
 * @recommendedaction
 *
 */


#define P_MSG_OPEN_FAILED                       (POSIX_COMP_BASE + 69)
/*!
 * @messageid
 * @diagnosis
 * @recommendedaction
 *
 */

#define P_MSG_LOOKUP_NOT_PERMITTED              (POSIX_COMP_BASE + 70)
/*!
 * @messageid
 * @diagnosis
 * @recommendedaction
 *
 */

#define P_MSG_RENAME_FAILED                     (POSIX_COMP_BASE + 71)
/*!
 * @messageid
 * @diagnosis
 * @recommendedaction
 *
 */

#define P_MSG_WRITE_FAILED                      (POSIX_COMP_BASE + 72)
/*!
 * @messageid
 * @diagnosis
 * @recommendedaction
 *
 */

#define P_MSG_FILE_FAILED                       (POSIX_COMP_BASE + 73)
/*!
 * @messageid
 * @diagnosis
 * @recommendedaction
 *
 */

#define P_MSG_THREAD_FAILED                     (POSIX_COMP_BASE + 74)
/*!
 * @messageid
 * @diagnosis
 * @recommendedaction
 *
 */

#define P_MSG_HEALTHCHECK_FAILED                (POSIX_COMP_BASE + 75)
/*!
 * @messageid
 * @diagnosis
 * @recommendedaction
 *
 */

#define P_MSG_GET_FDCTX_FAILED                  (POSIX_COMP_BASE + 76)
/*!
 * @messageid
 * @diagnosis
 * @recommendedaction
 *
 */

#define P_MSG_HANDLEPATH_FAILED                 (POSIX_COMP_BASE + 77)
/*!
 * @messageid
 * @diagnosis
 * @recommendedaction
 *
 */

#define P_MSG_IPC_NOT_HANDLE                    (POSIX_COMP_BASE + 78)
/*!
 * @messageid
 * @diagnosis
 * @recommendedaction
 *
 */

#define P_MSG_SET_XDATA_FAIL                    (POSIX_COMP_BASE + 79)
/*!
 * @messageid
 * @diagnosis
 * @recommendedaction
 *
 */

#define P_MSG_DURABILITY_REQ_NOT_SATISFIED      (POSIX_COMP_BASE + 80)
/*!
 * @messageid
 * @diagnosis
 * @recommendedaction
 *
 */

#define P_MSG_XATTR_NOTSUP                      (POSIX_COMP_BASE + 81)
/*!
 * @messageid
 * @diagnosis
 * @recommendedaction
 *
 */

#define P_MSG_GFID_SET_FAILED                   (POSIX_COMP_BASE + 82)
/*!
 * @messageid
 * @diagnosis
 * @recommendedaction
 *
 */

#define P_MSG_ACL_NOTSUP                        (POSIX_COMP_BASE + 83)
/*!
 * @messageid
 * @diagnosis
 * @recommendedaction
 *
 */

#define P_MSG_BASEPATH_CHDIR_FAILED             (POSIX_COMP_BASE + 84)
/*!
 * @messageid
 * @diagnosis
 * @recommendedaction
 *
 */

#define P_MSG_INVALID_OPTION_VAL                (POSIX_COMP_BASE + 85)
/*!
 * @messageid
 * @diagnosis
 * @recommendedaction
 *
 */

#define P_MSG_INVALID_NODE_UUID                 (POSIX_COMP_BASE + 86)
/*!
 * @messageid
 * @diagnosis
 * @recommendedaction
 *
 */

#define P_MSG_FSYNCER_THREAD_CREATE_FAILED      (POSIX_COMP_BASE + 87)
/*!
 * @messageid
 * @diagnosis
 * @recommendedaction
 *
 */

#define P_MSG_GF_DIRENT_CREATE_FAILED           (POSIX_COMP_BASE + 88)
/*!
 * @messageid
 * @diagnosis
 * @recommendedaction
 *
 */

#define P_MSG_VOLUME_ID_FETCH_FAILED            (POSIX_COMP_BASE + 89)
/*!
 * @messageid
 * @diagnosis
 * @recommendedaction
 *
 */

#define P_MSG_UNKNOWN_ARGUMENT                  (POSIX_COMP_BASE + 90)
/*!
 * @messageid
 * @diagnosis
 * @recommendedaction
 *
 */

#define P_MSG_INODE_HANDLE_CREATE               (POSIX_COMP_BASE + 91)
/*!
 * @messageid
 * @diagnosis
 * @recommendedaction
 *
 */

#define P_MSG_ENTRY_HANDLE_CREATE               (POSIX_COMP_BASE + 92)
/*!
 * @messageid
 * @diagnosis
 * @recommendedaction
 *
 */

#define P_MSG_PGFID_OP                          (POSIX_COMP_BASE + 93)
/*!
 * @messageid
 * @diagnosis
 * @recommendedaction
 *
 */

#define P_MSG_POSIX_AIO                         (POSIX_COMP_BASE + 94)
/*!
 * @messageid
 * @diagnosis
 * @recommendedaction
 *
 */

#define P_MSG_HANDLE_CREATE_TRASH               (POSIX_COMP_BASE + 95)
/*!
 * @messageid
 * @diagnosis
 * @recommendedaction
 *
 */

#define P_MSG_HANDLE_CREATE                     (POSIX_COMP_BASE + 96)
/*!
 * @messageid
 * @diagnosis
 * @recommendedaction
 *
 */

#define P_MSG_HANDLE_PATH_CREATE                (POSIX_COMP_BASE + 97)
/*!
 * @messageid
 * @diagnosis
 * @recommendedaction
 *
 */

#define P_MSG_SET_FILE_CONTENTS                 (POSIX_COMP_BASE + 98)
/*!
 * @messageid
 * @diagnosis
 * @recommendedaction
 *
 */

#define P_MSG_XDATA_GETXATTR                    (POSIX_COMP_BASE + 99)
/*!
 * @messageid
 * @diagnosis
 * @recommendedaction
 *
 */

#define P_MSG_STALE_HANDLE_REMOVE_FAILED        (POSIX_COMP_BASE + 100)
/*!
 * @messageid
 * @diagnosis
 * @recommendedaction
 *
 */

#define P_MSG_HANDLE_PATH_CREATE_FAILED         (POSIX_COMP_BASE + 101)
/*!
 * @messageid
 * @diagnosis
 * @recommendedaction
 *
 */

#define P_MSG_HANDLE_TRASH_CREATE               (POSIX_COMP_BASE + 102)
/*!
 * @messageid
 * @diagnosis
 * @recommendedaction
 *
 */

#define P_MSG_HANDLE_DELETE                     (POSIX_COMP_BASE + 103)
/*!
 * @messageid
 * @diagnosis
 * @recommendedaction
 *
 */

#define P_MSG_READLINK_FAILED                   (POSIX_COMP_BASE + 104)
/*!
 * @messageid
 * @diagnosis
 * @recommendedaction
 *
 */

#define P_MSG_BUFFER_OVERFLOW                   (POSIX_COMP_BASE + 105)
/*!
 * @messageid
 * @diagnosis
 * @recommendedaction
 *
 */

#define P_MSG_SEEK_UNKOWN                       (POSIX_COMP_BASE + 106)
/*!
 * @messageid
 * @diagnosis
 * @recommendedaction
 *
 */

#define P_MSG_SEEK_FAILED                       (POSIX_COMP_BASE + 107)
/*!
 * @messageid
 * @diagnosis
 * @recommendedaction
 *
 */

#define P_MSG_INODE_RESOLVE_FAILED              (POSIX_COMP_BASE + 108)

/*!
 * @messageid
 * @diagnosis
 * @recommendedaction
 *
 */

#define P_MSG_PREOP_CHECK_FAILED              (POSIX_COMP_BASE + 109)


/*!
 * @messageid
 * @diagnosis
 * @recommendedaction
 *
 */

#define P_MSG_LEASE_DISABLED                    (POSIX_COMP_BASE + 110)

/*!
 * @messageid
 * @diagnosis
 * @recommendedaction
 *
 */

#define P_MSG_ANCESTORY_FAILED                    (POSIX_COMP_BASE + 111)

/*!
 * @messageid
 * @diagnosis
 * @recommendedaction
 *
 */


/*------------*/
#define glfs_msg_end_x GLFS_MSGID_END, "Invalid: End of messages"

#endif /* !_GLUSTERD_MESSAGES_H_ */
