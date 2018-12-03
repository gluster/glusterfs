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
           SVC_MSG_PRIV_DESTROY_FAILED);

#endif /* !_SNAPVIEW_CLIENT_MESSAGES_H_ */
