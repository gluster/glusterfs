/*
 Copyright (c) 2014 Red Hat, Inc. <http://www.redhat.com>
 This file is part of GlusterFS.

 This file is licensed to you under your choice of the GNU Lesser
 General Public License, version 3 or any later version (LGPLv3 or
 later), or the GNU General Public License, version 2 (GPLv2), in all
 cases as published by the Free Software Foundation.
 */

#ifndef _AFR_MESSAGES_H_
#define _AFR_MESSAGES_H_

#ifndef _CONFIG_H
#define _CONFIG_H
#include "config.h"
#endif

#include "glfs-message-id.h"

/* NOTE: Rules for message additions
 * 1) Each instance of a message is _better_ left with a unique message ID, even
 * if the message format is the same. Reasoning is that, if the message
 * format needs to change in one instance, the other instances are not
 * impacted or the new change does not change the ID of the instance being
 * modified.
 * 2) Addition of a message,
 * - Should increment the GLFS_NUM_MESSAGES
 * - Append to the list of messages defined, towards the end
 * - Retain macro naming as glfs_msg_X (for redability across developers)
 * NOTE: Rules for message format modifications
 * 3) Check acorss the code if the message ID macro in question is reused
 * anywhere. If reused then then the modifications should ensure correctness
 * everywhere, or needs a new message ID as (1) above was not adhered to. If
 * not used anywhere, proceed with the required modification.
 * NOTE: Rules for message deletion
 * 4) Check (3) and if used anywhere else, then cannot be deleted. If not used
 * anywhere, then can be deleted, but will leave a hole by design, as
 * addition rules specify modification to the end of the list and not filling
 * holes.
 */

#define GLFS_COMP_BASE_AFR GLFS_MSGID_COMP_AFR
#define GLFS_NUM_MESSAGES 9
#define GLFS_MSGID_END (GLFS_COMP_BASE_AFR + GLFS_NUM_MESSAGES + 1)

#define glfs_msg_start_x GLFS_COMP_BASE_AFR, "Invalid: Start of messages"

#define AFR_MSG_QUORUM_FAIL             (GLFS_COMP_BASE_AFR + 1)
#define AFR_MSG_QUORUM_MET              (GLFS_COMP_BASE_AFR + 2)
#define AFR_MSG_QUORUM_OVERRIDE         (GLFS_COMP_BASE_AFR + 3)
#define AFR_MSG_INVALID_CHILD_UP        (GLFS_COMP_BASE_AFR + 4)
#define AFR_MSG_SUBVOL_UP               (GLFS_COMP_BASE_AFR + 5)
#define AFR_MSG_ALL_SUBVOLS_DOWN        (GLFS_COMP_BASE_AFR + 6)
#define AFR_MSG_ENTRY_UNLOCK_FAIL       (GLFS_COMP_BASE_AFR + 7)
#define AFR_MSG_SPLIT_BRAIN             (GLFS_COMP_BASE_AFR + 8)
#define AFR_MSG_OPEN_FAIL               (GLFS_COMP_BASE_AFR + 9)

#define glfs_msg_end_x GLFS_MSGID_END, "Invalid: End of messages"

#endif /* !_AFR_MESSAGES_H_ */
