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

#define GLFS_COMP_BASE          GLFS_MSGID_COMP_GLUSTERFSD
#define GLFS_NUM_MESSAGES       37
#define GLFS_MSGID_END          (GLFS_COMP_BASE + GLFS_NUM_MESSAGES + 1)
/* Messaged with message IDs */
#define glfs_msg_start_x GLFS_COMP_BASE, "Invalid: Start of messages"
/*------------*/
#define glusterfsd_msg_1 (GLFS_COMP_BASE + 1), "Could not create absolute" \
                        " mountpoint path"
#define glusterfsd_msg_2 (GLFS_COMP_BASE + 2), "Could not get current " \
                        "working directory"
#define glusterfsd_msg_3 (GLFS_COMP_BASE + 3), "failed to set mount-point" \
                        " to options dictionary"
#define glusterfsd_msg_4 (GLFS_COMP_BASE + 4), "failed to set dict value" \
                        " for key %s"
#define glusterfsd_msg_5 (GLFS_COMP_BASE + 5), "failed to set 'disable'" \
                        " for key %s"
#define glusterfsd_msg_6 (GLFS_COMP_BASE + 6), "failed to set 'enable'" \
                        " for key %s"
#define glusterfsd_msg_7 (GLFS_COMP_BASE + 7), "Not a client process, not" \
                        " performing mount operation"
#define glusterfsd_msg_8 (GLFS_COMP_BASE + 8), "MOUNT-POINT %s" \
                        " initialization failed"
#define glusterfsd_msg_9 (GLFS_COMP_BASE + 9), "loading volume file %s" \
                        " failed"
#define glusterfsd_msg_10 (GLFS_COMP_BASE + 10), "xlator option %s is" \
                        " invalid"
#define glusterfsd_msg_11 (GLFS_COMP_BASE + 11), "Fetching the volume" \
                        " file from server..."
#define glusterfsd_msg_12 (GLFS_COMP_BASE + 12), "volume initialization" \
                        " failed."
#define glusterfsd_msg_13 (GLFS_COMP_BASE + 13), "ERROR: glusterfs uuid" \
                        " generation failed"
#define glusterfsd_msg_14 (GLFS_COMP_BASE + 14), "ERROR: glusterfs %s" \
                        " pool creation failed"
#define glusterfsd_msg_15 (GLFS_COMP_BASE + 15), "ERROR: '--volfile-id' is" \
                        " mandatory if '-s' OR '--volfile-server'" \
                        " option is given"
#define glusterfsd_msg_16 (GLFS_COMP_BASE + 16), "ERROR: parsing the" \
                        " volfile failed"
#define glusterfsd_msg_17 (GLFS_COMP_BASE + 17), "pidfile %s open failed"
#define glusterfsd_msg_18 (GLFS_COMP_BASE + 18), "pidfile %s lock failed"
#define glusterfsd_msg_19 (GLFS_COMP_BASE + 19), "pidfile %s unlock failed"
#define glusterfsd_msg_20 (GLFS_COMP_BASE + 20), "pidfile %s truncation" \
                        " failed"
#define glusterfsd_msg_21 (GLFS_COMP_BASE + 21), "pidfile %s write failed"
#define glusterfsd_msg_22 (GLFS_COMP_BASE + 22), "failed to execute" \
                        " pthread_sigmask"
#define glusterfsd_msg_23 (GLFS_COMP_BASE + 23), "failed to create pthread"
#define glusterfsd_msg_24 (GLFS_COMP_BASE + 24), "daemonization failed"
#define glusterfsd_msg_25 (GLFS_COMP_BASE + 25), "mount failed"
#define glusterfsd_msg_26 (GLFS_COMP_BASE + 26), "failed to construct" \
                        " the graph"
#define glusterfsd_msg_27 (GLFS_COMP_BASE + 27), "fuse xlator cannot be" \
                        " specified in volume file"
#define glusterfsd_msg_28 (GLFS_COMP_BASE + 28), "Cannot reach volume" \
                        " specification file"
#define glusterfsd_msg_29 (GLFS_COMP_BASE + 29), "ERROR: glusterfs context" \
                        " not initialized"
#define glusterfsd_msg_30 (GLFS_COMP_BASE + 30), "Started running %s" \
                        " version %s (args: %s)"
#define glusterfsd_msg_31 (GLFS_COMP_BASE + 31), "Could not create new" \
                        " sync-environment"
#define glusterfsd_msg_32 (GLFS_COMP_BASE + 32), "received signum (%d)," \
                        " shutting down"
#define glusterfsd_msg_33 (GLFS_COMP_BASE + 33), "obsolete option " \
                        "'--volfile-max-fetch-attempts or fetch-attempts' " \
                        "was provided"
#define glusterfsd_msg_34 (GLFS_COMP_BASE + 34), "memory accounting init" \
                        " failed."
#define glusterfsd_msg_35 (GLFS_COMP_BASE + 35), "rpc req buffer " \
                        " unserialization failed."
#define glusterfsd_msg_36 (GLFS_COMP_BASE + 36), "problem in xlator " \
                        " loading."
#define glusterfsd_msg_37 (GLFS_COMP_BASE + 37), "failed to get dict value"

/*------------*/
#define glfs_msg_end_x GLFS_MSGID_END, "Invalid: End of messages"


#endif /* !_GLUSTERFSD_MESSAGES_H_ */
