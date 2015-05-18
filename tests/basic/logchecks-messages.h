/*
 Copyright (c) 2013 Red Hat, Inc. <http://www.redhat.com>
 This file is part of GlusterFS.

 This file is licensed to you under your choice of the GNU Lesser
 General Public License, version 3 or any later version (LGPLv3 or
 later), or the GNU General Public License, version 2 (GPLv2), in all
 cases as published by the Free Software Foundation.
 */

#ifndef _LOGCHECKS_MESSAGES_H_
#define _LOGCHECKS_MESSAGES_H_

#include "glfs-message-id.h"

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

#define GLFS_COMP_BASE          1000
#define GLFS_NUM_MESSAGES       19
#define GLFS_MSGID_END          (GLFS_COMP_BASE + GLFS_NUM_MESSAGES + 1)
/* Messaged with message IDs */
#define glfs_msg_start_x GLFS_COMP_BASE, "Invalid: Start of messages"
/*------------*/
#define logchecks_msg_1 (GLFS_COMP_BASE + 1), "Informational: Testing logging" \
                                " in gluster"
#define logchecks_msg_2 (GLFS_COMP_BASE + 2), "Informational: Format testing:" \
                                " %d:%s:%x"
#define logchecks_msg_3 (GLFS_COMP_BASE + 3), "Critical: Testing logging" \
                                " in gluster"
#define logchecks_msg_4 (GLFS_COMP_BASE + 4), "Critical: Format testing:" \
                                " %d:%s:%x"
#define logchecks_msg_5 (GLFS_COMP_BASE + 5), "Critical: Rotated the log"
#define logchecks_msg_6 (GLFS_COMP_BASE + 6), "Critical: Flushed the log"
#define logchecks_msg_7 (GLFS_COMP_BASE + 7), "Informational: gf_msg_callingfn"
#define logchecks_msg_8 (GLFS_COMP_BASE + 8), "Informational: " \
                                "gf_msg_callingfn: Format testing: %d:%s:%x"
#define logchecks_msg_9 (GLFS_COMP_BASE + 9), "Critical: gf_msg_callingfn"
#define logchecks_msg_10 (GLFS_COMP_BASE + 10), "Critical: " \
                                "gf_msg_callingfn: Format testing: %d:%s:%x"
#define logchecks_msg_11 (GLFS_COMP_BASE + 11), "=========================="
#define logchecks_msg_12 (GLFS_COMP_BASE + 12), "Test 1: Only stderr and" \
                                " partial syslog"
#define logchecks_msg_13 (GLFS_COMP_BASE + 13), "Test 2: Only checklog and" \
                                " partial syslog"
#define logchecks_msg_14 (GLFS_COMP_BASE + 14), "Test 5: Changing to" \
                                " traditional format"
#define logchecks_msg_15 (GLFS_COMP_BASE + 15), "Test 6: Changing log level" \
                                " to critical and above"
#define logchecks_msg_16 (GLFS_COMP_BASE + 16), "Test 7: Only to syslog"
#define logchecks_msg_17 (GLFS_COMP_BASE + 17), "Test 8: Only to syslog," \
                                " traditional format"
#define logchecks_msg_18 (GLFS_COMP_BASE + 18), "Test 9: Only to syslog," \
                                " only critical and above"
#define logchecks_msg_19 (GLFS_COMP_BASE + 19), "Pre init message, not to be" \
                                " seen in logs"
/*------------*/
#define glfs_msg_end_x GLFS_MSGID_END, "Invalid: End of messages"

#endif /* !_component_MESSAGES_H_ */