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

#include <glusterfs/glfs-message-id.h>

/* To add new message IDs, append new identifiers at the end of the list.
 *
 * Never remove a message ID. If it's not used anymore, you can rename it or
 * leave it as it is, but not delete it. This is to prevent reutilization of
 * IDs by other messages.
 *
 * The component name must match one of the entries defined in
 * glfs-message-id.h.
 */

GLFS_MSGID(
    PS, PS_MSG_AUTHENTICATE_ERROR, PS_MSG_VOL_VALIDATE_FAILED,
    PS_MSG_AUTH_INIT_FAILED, PS_MSG_REMOTE_CLIENT_REFUSED,
    PS_MSG_GFID_RESOLVE_FAILED, PS_MSG_ANONYMOUS_FD_CREATE_FAILED,
    PS_MSG_NO_MEMORY, PS_MSG_FD_NOT_FOUND, PS_MSG_INVALID_ENTRY,
    PS_MSG_GET_UID_FAILED, PS_MSG_UID_NOT_FOUND, PS_MSG_MAPPING_ERROR,
    PS_MSG_FD_CLEANUP, PS_MSG_SERVER_CTX_GET_FAILED, PS_MSG_FDENTRY_NULL,
    PS_MSG_DIR_NOT_FOUND, PS_MSG_SERVER_MSG, PS_MSG_DICT_SERIALIZE_FAIL,
    PS_MSG_RW_STAT, PS_MSG_DICT_GET_FAILED, PS_MSG_LOGIN_ERROR,
    PS_MSG_REMOUNT_CLIENT_REQD, PS_MSG_DEFAULTING_FILE,
    PS_MSG_VOL_FILE_OPEN_FAILED, PS_MSG_STAT_ERROR, PS_MSG_SSL_NAME_SET_FAILED,
    PS_MSG_ASPRINTF_FAILED, PS_MSG_CLIENT_VERSION_NOT_SET,
    PS_MSG_CLIENT_ACCEPTED, PS_MSG_CLIENT_LK_VERSION_ERROR,
    PS_MSG_GRACE_TIMER_EXPD, PS_MSG_SERIALIZE_REPLY_FAILED,
    PS_MSG_AUTH_IP_ERROR, PS_MSG_SKIP_FORMAT_CHK, PS_MSG_INTERNET_ADDR_ERROR,
    PS_MSG_CLIENT_DISCONNECTING, PS_MSG_GRACE_TIMER_START,
    PS_MSG_STATEDUMP_PATH_ERROR, PS_MSG_GRP_CACHE_ERROR, PS_MSG_RPC_CONF_ERROR,
    PS_MSG_TRANSPORT_ERROR, PS_MSG_SUBVOL_NULL, PS_MSG_PARENT_VOL_ERROR,
    PS_MSG_RPCSVC_CREATE_FAILED, PS_MSG_RPCSVC_LISTENER_CREATE_FAILED,
    PS_MSG_RPCSVC_NOTIFY, PS_MSG_PGM_REG_FAILED, PS_MSG_ULIMIT_SET_FAILED,
    PS_MSG_STATFS, PS_MSG_LOOKUP_INFO, PS_MSG_LK_INFO, PS_MSG_LOCK_ERROR,
    PS_MSG_INODELK_INFO, PS_MSG_ENTRYLK_INFO, PS_MSG_ACCESS_INFO,
    PS_MSG_DIR_INFO, PS_MSG_MKNOD_INFO, PS_MSG_REMOVEXATTR_INFO,
    PS_MSG_GETXATTR_INFO, PS_MSG_SETXATTR_INFO, PS_MSG_RENAME_INFO,
    PS_MSG_LINK_INFO, PS_MSG_TRUNCATE_INFO, PS_MSG_FSTAT_INFO,
    PS_MSG_FLUSH_INFO, PS_MSG_SYNC_INFO, PS_MSG_WRITE_INFO, PS_MSG_READ_INFO,
    PS_MSG_CHKSUM_INFO, PS_MSG_OPEN_INFO, PS_MSG_CREATE_INFO,
    PS_MSG_SETATTR_INFO, PS_MSG_XATTROP_INFO, PS_MSG_ALLOC_INFO,
    PS_MSG_DISCARD_INFO, PS_MSG_ZEROFILL_INFO, PS_MSG_FD_CREATE_FAILED,
    PS_MSG_WRONG_STATE, PS_MSG_CONF_DIR_INVALID, PS_MSG_MOUNT_PT_FAIL,
    PS_MSG_STAT_INFO, PS_MSG_FILE_OP_FAILED, PS_MSG_GRACE_TIMER_CANCELLED,
    PS_MSG_ENCODE_MSG_FAILED, PS_MSG_REPLY_SUBMIT_FAILED,
    PS_MSG_RPC_NOTIFY_ERROR, PS_MSG_SERVER_EVENT_UPCALL_FAILED,
    PS_MSG_SERVER_IPC_INFO, PS_MSG_SEEK_INFO, PS_MSG_COMPOUND_INFO,
    PS_MSG_CLIENT_OPVERSION_GET_FAILED, PS_MSG_CHILD_STATUS_FAILED,
    PS_MSG_PUT_INFO, PS_MSG_UNAUTHORIZED_CLIENT, PS_MSG_RECONFIGURE_FAILED,
    PS_MSG_SET_STATEDUMP_PATH_ERROR, PS_MSG_INIT_GRP_CACHE_ERROR,
    PS_MSG_RPC_CONFIGURE_FAILED, PS_MSG_TRANSPORT_TYPE_NOT_SET,
    PS_MSG_GET_TOTAL_AVAIL_TRANSPORT_FAILED, PS_MSG_INVLAID_UPCALL_EVENT,
    PS_MSG_SERVER_CHILD_EVENT_FAILED, PS_MSG_SETACTIVELK_INFO,
    PS_MSG_GETACTIVELK_INFO, PS_MSG_WRONG_VALUE, PS_MSG_PASSWORD_NOT_FOUND,
    PS_MSG_REMOTE_SUBVOL_NOT_SPECIFIED, PS_MSG_NO_MEM);

#define PS_MSG_SERIALIZE_REPLY_FAILED_STR "Failed to serialize reply"
#define PS_MSG_AUTH_IP_ERROR_STR "assuming 'auth.ip' to be 'auth.addr'"
#define PS_MSG_SKIP_FORMAT_CHK_STR "skip format check for non-addr auth option"
#define PS_MSG_INTERNET_ADDR_ERROR_STR                                         \
    "internet address does not confirm to standards"
#define PS_MSG_AUTHENTICATE_ERROR_STR                                          \
    "volume defined as subvolume, but no authentication defined for the same"
#define PS_MSG_CLIENT_DISCONNECTING_STR "disconnecting connection"
#define PS_MSG_DICT_GET_FAILED_STR "failed to get"
#define PS_MSG_NO_MEMORY_STR "Memory accounting init failed"
#define PS_MSG_INVALID_ENTRY_STR                                               \
    "'trace' takes on only boolean values. Neglecting option"
#define PS_MSG_STATEDUMP_PATH_ERROR_STR                                        \
    "Error while reconfiguring statedump path"
#define PS_MSG_GRP_CACHE_ERROR_STR "Failed to reconfigure group cache."
#define PS_MSG_RPC_CONF_ERROR_STR "No rpc_conf !!!!"
#define PS_MSG_CLIENT_ACCEPTED_STR                                             \
    "authorized client, hence we continue with this connection"
#define PS_MSG_UNAUTHORIZED_CLIENT_STR                                         \
    "unauthorized client, hence terminating the connection"
#define PS_MSG_RECONFIGURE_FAILED_STR                                          \
    "Failed to reconfigure outstanding-rpc-limit"
#define PS_MSG_TRANSPORT_ERROR_STR "Reconfigure not found for transport"
#define PS_MSG_SUBVOL_NULL_STR "protocol/server should have subvolume"
#define PS_MSG_PARENT_VOL_ERROR_STR                                            \
    "protocol/server should not have parent volumes"
#define PS_MSG_SET_STATEDUMP_PATH_ERROR_STR "Error setting statedump path"
#define PS_MSG_INIT_GRP_CACHE_ERROR_STR "Failed to initialize group cache."
#define PS_MSG_RPCSVC_CREATE_FAILED_STR "creation of rpcsvc failed"
#define PS_MSG_RPC_CONFIGURE_FAILED_STR                                        \
    "Failed to configure outstanding-rpc-limit"
#define PS_MSG_TRANSPORT_TYPE_NOT_SET_STR "option transport-type not set"
#define PS_MSG_GET_TOTAL_AVAIL_TRANSPORT_FAILED_STR                            \
    "failed to get total number of available tranpsorts"
#define PS_MSG_RPCSVC_LISTENER_CREATE_FAILED_STR "creation of listener failed"
#define PS_MSG_RPCSVC_NOTIFY_STR "registration of notify with rpcsvc failed"
#define PS_MSG_PGM_REG_FAILED_STR "registration of program failed"
#define PS_MSG_ULIMIT_SET_FAILED_STR "WARNING: Failed to set 'ulimit -n 1M'"
#define PS_MSG_FD_NOT_FOUND_STR "Failed to set max open fd to 64k"
#define PS_MSG_VOL_FILE_OPEN_FAILED_STR                                        \
    "volfile-id argument not given. This is mandatory argument, defaulting "   \
    "to 'gluster'"
#define PS_MSG_INVLAID_UPCALL_EVENT_STR "Received invalid upcall event"
#define PS_MSG_CHILD_STATUS_FAILED_STR "No xlator is found in child status list"
#define PS_MSG_SERVER_EVENT_UPCALL_FAILED_STR                                  \
    "server_process_event_upcall failed"
#define PS_MSG_SERVER_CHILD_EVENT_FAILED_STR "server_process_child_event failed"
#define PS_MSG_STATFS_STR "STATFS"
#define PS_MSG_LOOKUP_INFO_STR "LOOKUP info"
#define PS_MSG_LK_INFO_STR "LEASE info"
#define PS_MSG_INODELK_INFO_STR "INODELK info"
#define PS_MSG_DIR_INFO_STR "MKDIR info"
#define PS_MSG_MKNOD_INFO_STR "MKNOD info"
#define PS_MSG_REMOVEXATTR_INFO_STR "REMOVEXATTR info"
#define PS_MSG_GETXATTR_INFO_STR "GETXATTR info"
#define PS_MSG_SETXATTR_INFO_STR "SETXATTR info"
#define PS_MSG_RENAME_INFO_STR "RENAME inf"
#define PS_MSG_LINK_INFO_STR "LINK info"
#define PS_MSG_TRUNCATE_INFO_STR "TRUNCATE info"
#define PS_MSG_STAT_INFO_STR "STAT info"
#define PS_MSG_FLUSH_INFO_STR "FLUSH info"
#define PS_MSG_SYNC_INFO_STR "SYNC info"
#define PS_MSG_WRITE_INFO_STR "WRITE info"
#define PS_MSG_READ_INFO_STR "READ info"
#define PS_MSG_CHKSUM_INFO_STR "CHKSUM info"
#define PS_MSG_OPEN_INFO_STR "OPEN info"
#define PS_MSG_XATTROP_INFO_STR "XATTROP info"
#define PS_MSG_ALLOC_INFO_STR "ALLOC info"
#define PS_MSG_DISCARD_INFO_STR "DISCARD info"
#define PS_MSG_ZEROFILL_INFO_STR "ZEROFILL info"
#define PS_MSG_SERVER_IPC_INFO_STR "IPC info"
#define PS_MSG_SEEK_INFO_STR "SEEK info"
#define PS_MSG_SETACTIVELK_INFO_STR "SETACTIVELK info"
#define PS_MSG_CREATE_INFO_STR "CREATE info"
#define PS_MSG_PUT_INFO_STR "PUT info"
#define PS_MSG_FD_CREATE_FAILED_STR "could not create the fd"
#define PS_MSG_GETACTIVELK_INFO_STR "GETACTIVELK  info"
#define PS_MSG_ENTRYLK_INFO_STR "ENTRYLK info"
#define PS_MSG_ACCESS_INFO_STR "ACCESS info"
#define PS_MSG_SETATTR_INFO_STR "SETATTR info"
#define PS_MSG_SERVER_CTX_GET_FAILED_STR "server_ctx_get() failed"
#define PS_MSG_LOCK_ERROR_STR "Unknown lock type"
#define PS_MSG_GET_UID_FAILED_STR "getpwuid_r failed"
#define PS_MSG_UID_NOT_FOUND_STR "getpwuid_r found nothing"
#define PS_MSG_MAPPING_ERROR_STR "could not map to group list"
#define PS_MSG_FD_CLEANUP_STR "fd cleanup"
#define PS_MSG_FDENTRY_NULL_STR "no fdentry to clean"
#define PS_MSG_WRONG_VALUE_STR                                                 \
    "wrong value for 'verify-volfile-checksum', Neglecting option"
#define PS_MSG_DIR_NOT_FOUND_STR "Directory doesnot exist"
#define PS_MSG_CONF_DIR_INVALID_STR "invalid conf_dir"
#define PS_MSG_SERVER_MSG_STR "server msg"
#define PS_MSG_DICT_SERIALIZE_FAIL_STR "failed to serialize reply dict"
#define PS_MSG_MOUNT_PT_FAIL_STR "mount point fail"
#define PS_MSG_RW_STAT_STR "stat"
#define PS_MSG_PASSWORD_NOT_FOUND_STR "password not found, returning DONT-CARE"
#define PS_MSG_REMOTE_SUBVOL_NOT_SPECIFIED_STR "remote-subvolume not specified"
#define PS_MSG_LOGIN_ERROR_STR "wrong password for user"
#define PS_MSG_NO_MEM_STR "No memory"
#endif /* !_PS_MESSAGES_H__ */
