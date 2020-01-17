/*
 Copyright (c) 2018 Red Hat, Inc. <http://www.redhat.com>
 This file is part of GlusterFS.

 This file is licensed to you under your choice of the GNU Lesser
 General Public License, version 3 or any later version (LGPLv3 or
 later), or the GNU General Public License, version 2 (GPLv2), in all
 cases as published by the Free Software Foundation.
 */

#ifndef _SNAPVIEW_CLIENT_MESSAGES_H_
#define _SNAPVIEW_CLIENT_MESSAGES_H_

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

GLFS_MSGID(SNAPVIEW_CLIENT, SVC_MSG_NO_MEMORY, SVC_MSG_MEM_ACNT_FAILED,
           SVC_MSG_SET_INODE_CONTEXT_FAILED, SVC_MSG_GET_INODE_CONTEXT_FAILED,
           SVC_MSG_DELETE_INODE_CONTEXT_FAILED, SVC_MSG_SET_FD_CONTEXT_FAILED,
           SVC_MSG_GET_FD_CONTEXT_FAILED, SVC_MSG_DICT_SET_FAILED,
           SVC_MSG_SUBVOLUME_NULL, SVC_MSG_NO_CHILD_FOR_XLATOR,
           SVC_MSG_XLATOR_CHILDREN_WRONG, SVC_MSG_NORMAL_GRAPH_LOOKUP_FAIL,
           SVC_MSG_SNAPVIEW_GRAPH_LOOKUP_FAIL, SVC_MSG_OPENDIR_SPECIAL_DIR,
           SVC_MSG_RENAME_SNAPSHOT_ENTRY, SVC_MSG_LINK_SNAPSHOT_ENTRY,
           SVC_MSG_COPY_ENTRY_POINT_FAILED, SVC_MSG_ENTRY_POINT_SPECIAL_DIR,
           SVC_MSG_STR_LEN, SVC_MSG_INVALID_ENTRY_POINT, SVC_MSG_NULL_PRIV,
           SVC_MSG_PRIV_DESTROY_FAILED, SVC_MSG_ALLOC_FD_FAILED,
           SVC_MSG_ALLOC_INODE_FAILED, SVC_MSG_NULL_SPECIAL_DIR,
           SVC_MSG_MEM_POOL_GET_FAILED);

#define SVC_MSG_ALLOC_FD_FAILED_STR "failed to allocate new fd context"
#define SVC_MSG_SET_FD_CONTEXT_FAILED_STR "failed to set fd context"
#define SVC_MSG_STR_LEN_STR                                                    \
    "destination buffer size is less than the length of entry point name"
#define SVC_MSG_NORMAL_GRAPH_LOOKUP_FAIL_STR "lookup failed on normal graph"
#define SVC_MSG_SNAPVIEW_GRAPH_LOOKUP_FAIL_STR "lookup failed on snapview graph"
#define SVC_MSG_SET_INODE_CONTEXT_FAILED_STR "failed to set inode context"
#define SVC_MSG_NO_MEMORY_STR "failed to allocate memory"
#define SVC_MSG_COPY_ENTRY_POINT_FAILED_STR                                    \
    "failed to copy the entry point string"
#define SVC_MSG_GET_FD_CONTEXT_FAILED_STR "fd context not found"
#define SVC_MSG_GET_INODE_CONTEXT_FAILED_STR "failed to get inode context"
#define SVC_MSG_ALLOC_INODE_FAILED_STR "failed to allocate new inode"
#define SVC_MSG_DICT_SET_FAILED_STR "failed to set dict"
#define SVC_MSG_RENAME_SNAPSHOT_ENTRY_STR                                      \
    "rename happening on a entry residing in snapshot"
#define SVC_MSG_DELETE_INODE_CONTEXT_FAILED_STR "failed to delete inode context"
#define SVC_MSG_NULL_PRIV_STR "priv NULL"
#define SVC_MSG_INVALID_ENTRY_POINT_STR "not a valid entry point"
#define SVC_MSG_MEM_ACNT_FAILED_STR "Memory accouting init failed"
#define SVC_MSG_NO_CHILD_FOR_XLATOR_STR "configured without any child"
#define SVC_MSG_XLATOR_CHILDREN_WRONG_STR                                      \
    "snap-view-client has got wrong subvolumes. It can have only 2"
#define SVC_MSG_ENTRY_POINT_SPECIAL_DIR_STR                                    \
    "entry point directory cannot be part of special directory"
#define SVC_MSG_NULL_SPECIAL_DIR_STR "null special directory"
#define SVC_MSG_MEM_POOL_GET_FAILED_STR                                        \
    "could not get mem pool for frame->local"
#define SVC_MSG_PRIV_DESTROY_FAILED_STR "failed to destroy private"
#define SVC_MSG_LINK_SNAPSHOT_ENTRY_STR                                        \
    "link happening on a entry residin gin snapshot"
#endif /* !_SNAPVIEW_CLIENT_MESSAGES_H_ */
