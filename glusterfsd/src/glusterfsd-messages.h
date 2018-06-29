/*
  Copyright (c) 2013 Red Hat, Inc. <http://www.redhat.com>
  This file is part of GlusterFS.

  This file is licensed to you under your choice of the GNU Lesser
  General Public License, version 3 or any later version (LGPLv3 or
  later), or the GNU General Public License, version 2 (GPLv2), in all
  cases as published by the Free Software Foundation.
*/

#ifndef _GLUSTERFSD_MESSAGES_H_
#define _GLUSTERFSD_MESSAGES_H_

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

GLFS_MSGID(GLUSTERFSD,
        glusterfsd_msg_1,
        glusterfsd_msg_2,
        glusterfsd_msg_3,
        glusterfsd_msg_4,
        glusterfsd_msg_5,
        glusterfsd_msg_6,
        glusterfsd_msg_7,
        glusterfsd_msg_8,
        glusterfsd_msg_9,
        glusterfsd_msg_10,
        glusterfsd_msg_11,
        glusterfsd_msg_12,
        glusterfsd_msg_13,
        glusterfsd_msg_14,
        glusterfsd_msg_15,
        glusterfsd_msg_16,
        glusterfsd_msg_17,
        glusterfsd_msg_18,
        glusterfsd_msg_19,
        glusterfsd_msg_20,
        glusterfsd_msg_21,
        glusterfsd_msg_22,
        glusterfsd_msg_23,
        glusterfsd_msg_24,
        glusterfsd_msg_25,
        glusterfsd_msg_26,
        glusterfsd_msg_27,
        glusterfsd_msg_28,
        glusterfsd_msg_29,
        glusterfsd_msg_30,
        glusterfsd_msg_31,
        glusterfsd_msg_32,
        glusterfsd_msg_33,
        glusterfsd_msg_34,
        glusterfsd_msg_35,
        glusterfsd_msg_36,
        glusterfsd_msg_37,
        glusterfsd_msg_38
);

#endif /* !_GLUSTERFSD_MESSAGES_H_ */
