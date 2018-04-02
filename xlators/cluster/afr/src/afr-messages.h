/*
 Copyright (c) 2015 Red Hat, Inc. <http://www.redhat.com>
 This file is part of GlusterFS.

 This file is licensed to you under your choice of the GNU Lesser
 General Public License, version 3 or any later version (LGPLv3 or
 later), or the GNU General Public License, version 2 (GPLv2), in all
 cases as published by the Free Software Foundation.
 */

#ifndef _AFR_MESSAGES_H_
#define _AFR_MESSAGES_H_

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

GLFS_MSGID(AFR,
        AFR_MSG_QUORUM_FAIL,
        AFR_MSG_QUORUM_MET,
        AFR_MSG_QUORUM_OVERRIDE,
        AFR_MSG_INVALID_CHILD_UP,
        AFR_MSG_SUBVOL_UP,
        AFR_MSG_SUBVOLS_DOWN,
        AFR_MSG_ENTRY_UNLOCK_FAIL,
        AFR_MSG_SPLIT_BRAIN,
        AFR_MSG_OPEN_FAIL,
        AFR_MSG_UNLOCK_FAIL,
        AFR_MSG_REPLACE_BRICK_STATUS,
        AFR_MSG_GFID_NULL,
        AFR_MSG_FD_CREATE_FAILED,
        AFR_MSG_DICT_SET_FAILED,
        AFR_MSG_EXPUNGING_FILE_OR_DIR,
        AFR_MSG_MIGRATION_IN_PROGRESS,
        AFR_MSG_CHILD_MISCONFIGURED,
        AFR_MSG_VOL_MISCONFIGURED,
        AFR_MSG_BLOCKING_LKS_FAILED,
        AFR_MSG_INVALID_FD,
        AFR_MSG_LOCK_INFO,
        AFR_MSG_LOCK_XLATOR_NOT_LOADED,
        AFR_MSG_FD_CTX_GET_FAILED,
        AFR_MSG_INVALID_SUBVOL,
        AFR_MSG_PUMP_XLATOR_ERROR,
        AFR_MSG_SELF_HEAL_INFO,
        AFR_MSG_READ_SUBVOL_ERROR,
        AFR_MSG_DICT_GET_FAILED,
        AFR_MSG_INFO_COMMON,
        AFR_MSG_SPLIT_BRAIN_CHOICE_ERROR,
        AFR_MSG_LOCAL_CHILD,
        AFR_MSG_INVALID_DATA,
        AFR_MSG_INVALID_ARG,
        AFR_MSG_INDEX_DIR_GET_FAILED,
        AFR_MSG_FSYNC_FAILED,
        AFR_MSG_FAVORITE_CHILD,
        AFR_MSG_SELF_HEAL_FAILED,
        AFR_MSG_SPLIT_BRAIN_STATUS,
        AFR_MSG_ADD_BRICK_STATUS,
        AFR_MSG_NO_CHANGELOG,
        AFR_MSG_TIMER_CREATE_FAIL,
        AFR_MSG_SBRAIN_FAV_CHILD_POLICY,
        AFR_MSG_INODE_CTX_GET_FAILED,
        AFR_MSG_THIN_ARB
);

#endif /* !_AFR_MESSAGES_H_ */
