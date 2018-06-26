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

#include "glfs-message-id.h"

/* To add new message IDs, append new identifiers at the end of the list.
 *
 * Never remove a message ID. If it's not used anymore, you can rename it or
 * leave it as it is, but not delete it. This is to prevent reutilization of
 * IDs by other messages.
 *
 * The component name must match one of the entries defined in
 * glfs-message-id.h.
 */

GLFS_MSGID(POSIX,
        P_MSG_XATTR_FAILED,
        P_MSG_NULL_GFID,
        P_MSG_FCNTL_FAILED,
        P_MSG_READV_FAILED,
        P_MSG_FSTAT_FAILED,
        P_MSG_PFD_NULL,
        P_MSG_INVALID_ARGUMENT,
        P_MSG_IO_SUBMIT_FAILED,
        P_MSG_WRITEV_FAILED,
        P_MSG_IO_GETEVENTS_FAILED,
        P_MSG_UNKNOWN_OP,
        P_MSG_AIO_UNAVAILABLE,
        P_MSG_IO_SETUP_FAILED,
        P_MSG_ZEROFILL_FAILED,
        P_MSG_OPENDIR_FAILED,
        P_MSG_DIRFD_FAILED,
        P_MSG_FD_PATH_SETTING_FAILED,
        P_MSG_LSTAT_FAILED,
        P_MSG_READYLINK_FAILED,
        P_MSG_GFID_FAILED,
        P_MSG_CREATE_FAILED,
        P_MSG_MKNOD_FAILED,
        P_MSG_LCHOWN_FAILED,
        P_MSG_ACL_FAILED,
        P_MSG_MKDIR_NOT_PERMITTED,
        P_MSG_DIR_OF_SAME_ID,
        P_MSG_MKDIR_FAILED,
        P_MSG_CHOWN_FAILED,
        P_MSG_UNLINK_FAILED,
        P_MSG_KEY_STATUS_INFO,
        P_MSG_XATTR_STATUS,
        P_MSG_RMDIR_NOT_PERMITTED,
        P_MSG_RMDIR_FAILED,
        P_MSG_DIR_OPERATION_FAILED,
        P_MSG_SYMLINK_FAILED,
        P_MSG_DIR_FOUND,
        P_MSG_LINK_FAILED,
        P_MSG_TRUNCATE_FAILED,
        P_MSG_FILE_OP_FAILED,
        P_MSG_READ_FAILED,
        P_MSG_DICT_SET_FAILED,
        P_MSG_STATVFS_FAILED,
        P_MSG_DIR_NOT_NULL,
        P_MSG_FSYNC_FAILED,
        P_MSG_CLOSE_FAILED,
        P_MSG_GETTING_FILENAME_FAILED,
        P_MSG_INODE_PATH_GET_FAILED,
        P_MSG_GET_KEY_VALUE_FAILED,
        P_MSG_CHMOD_FAILED,
        P_MSG_FCHMOD_FAILED,
        P_MSG_FCHOWN_FAILED,
        P_MSG_UTIMES_FAILED,
        P_MSG_FUTIMES_FAILED,
        P_MSG_XATTR_NOT_REMOVED,
        P_MSG_PFD_GET_FAILED,
        P_MSG_ACCESS_FAILED,
        P_MSG_PREAD_FAILED,
        P_MSG_UUID_NULL,
        P_MSG_EXPORT_DIR_MISSING,
        P_MSG_SUBVOLUME_ERROR,
        P_MSG_VOLUME_DANGLING,
        P_MSG_INVALID_OPTION,
        P_MSG_INVALID_VOLUME_ID,
        P_MSG_VOLUME_ID_ABSENT,
        P_MSG_HOSTNAME_MISSING,
        P_MSG_SET_ULIMIT_FAILED,
        P_MSG_SET_FILE_MAX_FAILED,
        P_MSG_MAX_FILE_OPEN,
        P_MSG_OPEN_FAILED,
        P_MSG_LOOKUP_NOT_PERMITTED,
        P_MSG_RENAME_FAILED,
        P_MSG_WRITE_FAILED,
        P_MSG_FILE_FAILED,
        P_MSG_THREAD_FAILED,
        P_MSG_HEALTHCHECK_FAILED,
        P_MSG_GET_FDCTX_FAILED,
        P_MSG_HANDLEPATH_FAILED,
        P_MSG_IPC_NOT_HANDLE,
        P_MSG_SET_XDATA_FAIL,
        P_MSG_DURABILITY_REQ_NOT_SATISFIED,
        P_MSG_XATTR_NOTSUP,
        P_MSG_GFID_SET_FAILED,
        P_MSG_ACL_NOTSUP,
        P_MSG_BASEPATH_CHDIR_FAILED,
        P_MSG_INVALID_OPTION_VAL,
        P_MSG_INVALID_NODE_UUID,
        P_MSG_FSYNCER_THREAD_CREATE_FAILED,
        P_MSG_GF_DIRENT_CREATE_FAILED,
        P_MSG_VOLUME_ID_FETCH_FAILED,
        P_MSG_UNKNOWN_ARGUMENT,
        P_MSG_INODE_HANDLE_CREATE,
        P_MSG_ENTRY_HANDLE_CREATE,
        P_MSG_PGFID_OP,
        P_MSG_POSIX_AIO,
        P_MSG_HANDLE_CREATE_TRASH,
        P_MSG_HANDLE_CREATE,
        P_MSG_HANDLE_PATH_CREATE,
        P_MSG_SET_FILE_CONTENTS,
        P_MSG_XDATA_GETXATTR,
        P_MSG_STALE_HANDLE_REMOVE_FAILED,
        P_MSG_HANDLE_PATH_CREATE_FAILED,
        P_MSG_HANDLE_TRASH_CREATE,
        P_MSG_HANDLE_DELETE,
        P_MSG_READLINK_FAILED,
        P_MSG_BUFFER_OVERFLOW,
        P_MSG_SEEK_UNKOWN,
        P_MSG_SEEK_FAILED,
        P_MSG_INODE_RESOLVE_FAILED,
        P_MSG_PREOP_CHECK_FAILED,
        P_MSG_LEASE_DISABLED,
        P_MSG_ANCESTORY_FAILED,
        P_MSG_DISK_SPACE_CHECK_FAILED,
        P_MSG_FALLOCATE_FAILED,
        P_MSG_STOREMDATA_FAILED,
        P_MSG_FETCHMDATA_FAILED,
        P_MSG_GETMDATA_FAILED,
        P_MSG_SETMDATA_FAILED,
        P_MSG_FRESHFILE
);

#endif /* !_GLUSTERD_MESSAGES_H_ */
