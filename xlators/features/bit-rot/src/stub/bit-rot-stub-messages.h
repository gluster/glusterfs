/*
 Copyright (c) 2015 Red Hat, Inc. <http://www.redhat.com>
 This file is part of GlusterFS.

 This file is licensed to you under your choice of the GNU Lesser
 General Public License, version 3 or any later version (LGPLv3 or
 later), or the GNU General Public License, version 2 (GPLv2), in all
 cases as published by the Free Software Foundation.
 */

#ifndef _BITROT_STUB_MESSAGES_H_
#define _BITROT_STUB_MESSAGES_H_

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

GLFS_MSGID(BITROT_STUB, BRS_MSG_NO_MEMORY, BRS_MSG_SET_EVENT_FAILED,
           BRS_MSG_MEM_ACNT_FAILED, BRS_MSG_CREATE_FRAME_FAILED,
           BRS_MSG_SET_CONTEXT_FAILED, BRS_MSG_CHANGE_VERSION_FAILED,
           BRS_MSG_ADD_FD_TO_LIST_FAILED, BRS_MSG_SET_FD_CONTEXT_FAILED,
           BRS_MSG_CREATE_ANONYMOUS_FD_FAILED, BRS_MSG_NO_CHILD,
           BRS_MSG_STUB_ALLOC_FAILED, BRS_MSG_GET_INODE_CONTEXT_FAILED,
           BRS_MSG_CANCEL_SIGN_THREAD_FAILED, BRS_MSG_ADD_FD_TO_INODE,
           BRS_MSG_SIGN_VERSION_ERROR, BRS_MSG_BAD_OBJ_MARK_FAIL,
           BRS_MSG_NON_SCRUB_BAD_OBJ_MARK, BRS_MSG_REMOVE_INTERNAL_XATTR,
           BRS_MSG_SET_INTERNAL_XATTR, BRS_MSG_BAD_OBJECT_ACCESS,
           BRS_MSG_BAD_CONTAINER_FAIL, BRS_MSG_BAD_OBJECT_DIR_FAIL,
           BRS_MSG_BAD_OBJECT_DIR_SEEK_FAIL, BRS_MSG_BAD_OBJECT_DIR_TELL_FAIL,
           BRS_MSG_BAD_OBJECT_DIR_READ_FAIL, BRS_MSG_GET_FD_CONTEXT_FAILED,
           BRS_MSG_BAD_HANDLE_DIR_NULL, BRS_MSG_BAD_OBJ_THREAD_FAIL,
           BRS_MSG_BAD_OBJ_DIR_CLOSE_FAIL, BRS_MSG_LINK_FAIL,
           BRS_MSG_BAD_OBJ_UNLINK_FAIL, BRS_MSG_DICT_SET_FAILED,
           BRS_MSG_PATH_GET_FAILED, BRS_MSG_NULL_LOCAL,
           BRS_MSG_SPAWN_SIGN_THRD_FAILED, BRS_MSG_KILL_SIGN_THREAD,
           BRS_MSG_NON_BITD_PID, BRS_MSG_SIGN_PREPARE_FAIL,
           BRS_MSG_USING_DEFAULT_THREAD_SIZE, BRS_MSG_ALLOC_MEM_FAILED,
           BRS_MSG_DICT_ALLOC_FAILED, BRS_MSG_CREATE_GF_DIRENT_FAILED,
           BRS_MSG_ALLOC_FAILED, BRS_MSG_PATH_XATTR_GET_FAILED,
           BRS_MSG_VERSION_PREPARE_FAIL);

#define BRS_MSG_MEM_ACNT_FAILED_STR "Memory accounting init failed"
#define BRS_MSG_BAD_OBJ_THREAD_FAIL_STR "pthread_init failed"
#define BRS_MSG_USING_DEFAULT_THREAD_SIZE_STR "Using default thread stack size"
#define BRS_MSG_NO_CHILD_STR "FATAL: no children"
#define BRS_MSG_SPAWN_SIGN_THRD_FAILED_STR                                     \
    "failed to create the new thread for signer"
#define BRS_MSG_BAD_CONTAINER_FAIL_STR                                         \
    "failed to launch the thread for storing bad gfids"
#define BRS_MSG_CANCEL_SIGN_THREAD_FAILED_STR                                  \
    "Could not cancel sign serializer thread"
#define BRS_MSG_KILL_SIGN_THREAD_STR "killed the signer thread"
#define BRS_MSG_GET_INODE_CONTEXT_FAILED_STR                                   \
    "failed to init the inode context for the inode"
#define BRS_MSG_ADD_FD_TO_INODE_STR "failed to add fd to the inode"
#define BRS_MSG_NO_MEMORY_STR "local allocation failed"
#define BRS_MSG_BAD_OBJECT_ACCESS_STR "bad object accessed. Returning"
#define BRS_MSG_SIGN_VERSION_ERROR_STR "Signing version exceeds current version"
#define BRS_MSG_NON_BITD_PID_STR                                               \
    "PID from where signature request came, does not belong to bit-rot "       \
    "daemon. Unwinding the fop"
#define BRS_MSG_SIGN_PREPARE_FAIL_STR                                          \
    "failed to prepare the signature. Unwinding the fop"
#define BRS_MSG_VERSION_PREPARE_FAIL_STR                                       \
    "failed to prepare the version. Unwinding the fop"
#define BRS_MSG_STUB_ALLOC_FAILED_STR "failed to allocate stub fop, Unwinding"
#define BRS_MSG_BAD_OBJ_MARK_FAIL_STR "failed to mark object as bad"
#define BRS_MSG_NON_SCRUB_BAD_OBJ_MARK_STR                                     \
    "bad object marking is not from the scrubber"
#define BRS_MSG_ALLOC_MEM_FAILED_STR "failed to allocate memory"
#define BRS_MSG_SET_INTERNAL_XATTR_STR "called on the internal xattr"
#define BRS_MSG_REMOVE_INTERNAL_XATTR_STR "removexattr called on internal xattr"
#define BRS_MSG_CREATE_ANONYMOUS_FD_FAILED_STR                                 \
    "failed to create anonymous fd for the inode"
#define BRS_MSG_ADD_FD_TO_LIST_FAILED_STR "failed add fd to the list"
#define BRS_MSG_SET_FD_CONTEXT_FAILED_STR                                      \
    "failed to set the fd context for the file"
#define BRS_MSG_NULL_LOCAL_STR "local is NULL"
#define BRS_MSG_DICT_ALLOC_FAILED_STR                                          \
    "dict allocation failed: cannot send IPC FOP to changelog"
#define BRS_MSG_SET_EVENT_FAILED_STR "cannot set release event in dict"
#define BRS_MSG_CREATE_FRAME_FAILED_STR "create_frame() failure"
#define BRS_MSG_BAD_OBJ_DIR_CLOSE_FAIL_STR "closedir error"
#define BRS_MSG_LINK_FAIL_STR "failed to record gfid"
#define BRS_MSG_BAD_OBJ_UNLINK_FAIL_STR                                        \
    "failed to delete bad object link from quaratine directory"
#define BRS_MSG_BAD_OBJECT_DIR_FAIL_STR "failed stub directory"
#define BRS_MSG_BAD_OBJECT_DIR_SEEK_FAIL_STR                                   \
    "seekdir failed. Invalid argument (offset reused from another DIR * "      \
    "structure)"
#define BRS_MSG_BAD_OBJECT_DIR_TELL_FAIL_STR "telldir failed on dir"
#define BRS_MSG_BAD_OBJECT_DIR_READ_FAIL_STR "readdir failed on dir"
#define BRS_MSG_CREATE_GF_DIRENT_FAILED_STR "could not create gf_dirent"
#define BRS_MSG_GET_FD_CONTEXT_FAILED_STR "pfd is NULL"
#define BRS_MSG_BAD_HANDLE_DIR_NULL_STR "dir if NULL"
#define BRS_MSG_ALLOC_FAILED_STR                                               \
    "failed to allocate new dict for saving the paths of the corrupted "       \
    "objects. Scrub status will only display the gfid"
#define BRS_MSG_PATH_GET_FAILED_STR "failed to get the path"
#define BRS_MSG_PATH_XATTR_GET_FAILED_STR                                      \
    "failed to get the path xattr from disk for the gfid. Trying to get path " \
    "from the memory"
#define BRS_MSG_DICT_SET_FAILED_STR                                            \
    "failed to set the actual path as the value in the dict for the "          \
    "corrupted object"
#define BRS_MSG_SET_CONTEXT_FAILED_STR                                         \
    "could not set fd context for release callback"
#define BRS_MSG_CHANGE_VERSION_FAILED_STR "change version failed"
#endif /* !_BITROT_STUB_MESSAGES_H_ */
