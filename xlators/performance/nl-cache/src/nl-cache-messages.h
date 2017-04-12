/*
 *   Copyright (c) 2016 Red Hat, Inc. <http://www.redhat.com>
 *   This file is part of GlusterFS.
 *
 *   This file is licensed to you under your choice of the GNU Lesser
 *   General Public License, version 3 or any later version (LGPLv3 or
 *   later), or the GNU General Public License, version 2 (GPLv2), in all
 *   cases as published by the Free Software Foundation.
 */


#ifndef __NL_CACHE_MESSAGES_H__
#define __NL_CACHE_MESSAGES_H__


#define GLFS_COMP_BASE_NLC GLFS_MSGID_COMP_NLC
#define GLFS_NUM_MESSAGES 4
#define GLFS_MSGID_END (GLFS_COMP_BASE_NLC + GLFS_NUM_MESSAGES + 1)

#define glfs_msg_start_x GLFS_COMP_BASE_NLC, "Invalid: Start of messages"

/*!
 * @messageid 110001
 * @diagnosis Out of Memory
 * @recommendedaction None
 */
#define NLC_MSG_NO_MEMORY             (GLFS_COMP_BASE_NLC + 1)
#define NLC_MSG_EINVAL                (GLFS_COMP_BASE_NLC + 2)
#define NLC_MSG_NO_TIMER_WHEEL        (GLFS_COMP_BASE_NLC + 3)
#define NLC_MSG_DICT_FAILURE          (GLFS_COMP_BASE_NLC + 4)
#define glfs_msg_end_x GLFS_MSGID_END, "Invalid: End of messages"


#endif /* __NL_CACHE_MESSAGES_H__ */
