/*
 *   Copyright (c) 2015 Red Hat, Inc. <http://www.redhat.com>
 *   This file is part of GlusterFS.
 *
 *   This file is licensed to you under your choice of the GNU Lesser
 *   General Public License, version 3 or any later version (LGPLv3 or
 *   later), or the GNU General Public License, version 2 (GPLv2), in all
 *   cases as published by the Free Software Foundation.
 *   */

#ifndef _GFAPI_MESSAGES_H__
#define _GFAPI_MESSAGES_H__

#include "glfs-message-id.h"

/*! \file gfapi-messages.h
 *  \brief libgfapi log-message IDs and their descriptions
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

#define GLFS_GFAPI_BASE             GLFS_MSGID_COMP_API
#define GLFS_NUM_MESSAGES           49
#define GLFS_MSGID_END              (GLFS_GFAPI_BASE + GLFS_NUM_MESSAGES + 1)
/* Messages with message IDs */
#define glfs_msg_start_x GLFS_GFAPI_BASE, "Invalid: Start of messages"
/*------------*/

#define API_MSG_MEM_ACCT_INIT_FAILED            (GLFS_GFAPI_BASE + 1)
#define API_MSG_MASTER_XLATOR_INIT_FAILED       (GLFS_GFAPI_BASE + 2)
#define API_MSG_GFAPI_XLATOR_INIT_FAILED        (GLFS_GFAPI_BASE + 3)
#define API_MSG_VOLFILE_OPEN_FAILED             (GLFS_GFAPI_BASE + 4)
#define API_MSG_VOL_SPEC_FILE_ERROR             (GLFS_GFAPI_BASE + 5)
#define API_MSG_GLFS_FSOBJ_NULL                 (GLFS_GFAPI_BASE + 6)
#define API_MSG_INVALID_ENTRY                   (GLFS_GFAPI_BASE + 7)
#define API_MSG_FSMUTEX_LOCK_FAILED             (GLFS_GFAPI_BASE + 8)
#define API_MSG_COND_WAIT_FAILED                (GLFS_GFAPI_BASE + 9)
#define API_MSG_FSMUTEX_UNLOCK_FAILED           (GLFS_GFAPI_BASE + 10)
#define API_MSG_INODE_REFRESH_FAILED            (GLFS_GFAPI_BASE + 11)
#define API_MSG_GRAPH_CONSTRUCT_FAILED          (GLFS_GFAPI_BASE + 12)
#define API_MSG_FUSE_XLATOR_ERROR               (GLFS_GFAPI_BASE + 13)
#define API_MSG_XDR_PAYLOAD_FAILED              (GLFS_GFAPI_BASE + 14)
#define API_MSG_GET_VOLINFO_CBK_FAILED          (GLFS_GFAPI_BASE + 15)
#define API_MSG_FETCH_VOLUUID_FAILED            (GLFS_GFAPI_BASE + 16)
#define API_MSG_INSUFF_SIZE                     (GLFS_GFAPI_BASE + 17)
#define API_MSG_FRAME_CREAT_FAILED              (GLFS_GFAPI_BASE + 18)
#define API_MSG_DICT_SET_FAILED                 (GLFS_GFAPI_BASE + 19)
#define API_MSG_XDR_DECODE_FAILED               (GLFS_GFAPI_BASE + 20)
#define API_MSG_GET_VOLFILE_FAILED              (GLFS_GFAPI_BASE + 21)
#define API_MSG_WRONG_OPVERSION                 (GLFS_GFAPI_BASE + 22)
#define API_MSG_DICT_SERIALIZE_FAILED           (GLFS_GFAPI_BASE + 23)
#define API_MSG_REMOTE_HOST_CONN_FAILED         (GLFS_GFAPI_BASE + 24)
#define API_MSG_VOLFILE_SERVER_EXHAUST          (GLFS_GFAPI_BASE + 25)
#define API_MSG_CREATE_RPC_CLIENT_FAILED        (GLFS_GFAPI_BASE + 26)
#define API_MSG_REG_NOTIFY_FUNC_FAILED          (GLFS_GFAPI_BASE + 27)
#define API_MSG_REG_CBK_FUNC_FAILED             (GLFS_GFAPI_BASE + 28)
#define API_MSG_GET_CWD_FAILED                  (GLFS_GFAPI_BASE + 29)
#define API_MSG_FGETXATTR_FAILED                (GLFS_GFAPI_BASE + 30)
#define API_MSG_LOCKINFO_KEY_MISSING            (GLFS_GFAPI_BASE + 31)
#define API_MSG_FSETXATTR_FAILED                (GLFS_GFAPI_BASE + 32)
#define API_MSG_FSYNC_FAILED                    (GLFS_GFAPI_BASE + 33)
#define API_MSG_FDCREATE_FAILED                 (GLFS_GFAPI_BASE + 34)
#define API_MSG_INODE_PATH_FAILED               (GLFS_GFAPI_BASE + 35)
#define API_MSG_SYNCOP_OPEN_FAILED              (GLFS_GFAPI_BASE + 36)
#define API_MSG_LOCK_MIGRATE_FAILED             (GLFS_GFAPI_BASE + 37)
#define API_MSG_OPENFD_SKIPPED                  (GLFS_GFAPI_BASE + 38)
#define API_MSG_FIRST_LOOKUP_GRAPH_FAILED       (GLFS_GFAPI_BASE + 39)
#define API_MSG_CWD_GRAPH_REF_FAILED            (GLFS_GFAPI_BASE + 40)
#define API_MSG_SWITCHED_GRAPH                  (GLFS_GFAPI_BASE + 41)
#define API_MSG_XDR_RESPONSE_DECODE_FAILED      (GLFS_GFAPI_BASE + 42)
#define API_MSG_VOLFILE_INFO                    (GLFS_GFAPI_BASE + 43)
#define API_MSG_VOLFILE_CONNECTING              (GLFS_GFAPI_BASE + 44)
#define API_MSG_NEW_GRAPH                       (GLFS_GFAPI_BASE + 45)
#define API_MSG_ALLOC_FAILED                    (GLFS_GFAPI_BASE + 46)
#define API_MSG_CREATE_HANDLE_FAILED            (GLFS_GFAPI_BASE + 47)
#define API_MSG_INODE_LINK_FAILED               (GLFS_GFAPI_BASE + 48)
#define API_MSG_STATEDUMP_FAILED                (GLFS_GFAPI_BASE + 49)

/*------------*/
#define glfs_msg_end_x GLFS_MSGID_END, "Invalid: End of messages"

#endif /* !_GFAPI_MESSAGES_H__ */
