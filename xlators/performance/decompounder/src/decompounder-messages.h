/*
  Copyright (c) 2016 Red Hat, Inc. <http://www.redhat.com>
  This file is part of GlusterFS.

  This file is licensed to you under your choice of the GNU Lesser
  General Public License, version 3 or any later version (LGPLv3 or
  later), or the GNU General Public License, version 2 (GPLv2), in all
  cases as published by the Free Software Foundation.
*/

#ifndef _DC_MESSAGES_H_
#define _DC_MESSAGES_H_

#include "glfs-message-id.h"

#define GLFS_COMP_BASE_DC      GLFS_MSGID_COMP_DC
#define GLFS_NUM_MESSAGES           2
#define GLFS_MSGID_END         (GLFS_COMP_BASE_DC + GLFS_NUM_MESSAGES + 1)

#define glfs_msg_start_x GLFS_COMP_BASE_DC, "Invalid: Start of messages"

#define DC_MSG_VOL_MISCONFIGURED               (GLFS_COMP_BASE_DC + 1)

#define DC_MSG_ERROR_RECEIVED                  (GLFS_COMP_BASE_DC + 2)

#define glfs_msg_end_x GLFS_MSGID_END, "Invalid: End of messages"
#endif /* !_DC_MESSAGES_H_ */
