/*
 Copyright (c) 2013-2022 Red Hat, Inc. <https://www.redhat.com>
 This file is part of GlusterFS.

 This file is licensed to you under your choice of the GNU Lesser
 General Public License, version 3 or any later version (LGPLv3 or
 later), or the GNU General Public License, version 2 (GPLv2), in all
 cases as published by the Free Software Foundation.
 */

#ifndef _component_MESSAGES_H_
#define _component_MESSAGES_H_

#include "glusterfs/glfs-message-id.h"

/* First of all the new component needs to be declared at the end of
 * enum _msgid_comp in glusterfs/glfs-message-id.h. Then the ID used
 * needs to be referenced here to start defining messages associated
 * with this component.
 *
 * More than one component can be defined, but its messages need to
 * be defined sequentially. There can't be definitions of messages
 * from different components interleaved. */

/* Example:
 *
 *    GLFS_COMPONENT(COMPONENT);
 */

/* Add every new message at the end. The position of the message
 * determines its ID, so adding the message at the beginning would
 * change the IDs of all other messages. Also never remove one message
 * once it has been present in at least one release. This would cause
 * the same message id to be reused by another message.
 *
 * For new messages, use GLFS_NEW(). To deprecate a message, leave
 * it as it is, but change GLFS_NEW by GLFS_OLD. To remove a message
 * (i.e. the message cannot be used by the code), replace GLFS_OLD
 * by GLFS_GONE. */

// clang-format off

/* Example:
 *
 *    GLFS_NEW(_COMPONENT, MSGID, "Message text", <num fields>
 *        GLFS_INT(integer),
 *        GLFS_UINT(number),
 *        GLFS_STR(name),
 *        GLFS_UUID(gfid),
 *        GLFS_PTR(pointer),
 *        GLFS_ERR(error),
 *        GLFS_RES(result)
 *    )
 */

/* Add new messages above this line. */

// clang-format on

#endif /* _component_MESSAGES_H_ */
