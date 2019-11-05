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

GLFS_MSGID(
    GLUSTERFSD, glusterfsd_msg_1, glusterfsd_msg_2, glusterfsd_msg_3,
    glusterfsd_msg_4, glusterfsd_msg_5, glusterfsd_msg_6, glusterfsd_msg_7,
    glusterfsd_msg_8, glusterfsd_msg_9, glusterfsd_msg_10, glusterfsd_msg_11,
    glusterfsd_msg_12, glusterfsd_msg_13, glusterfsd_msg_14, glusterfsd_msg_15,
    glusterfsd_msg_16, glusterfsd_msg_17, glusterfsd_msg_18, glusterfsd_msg_19,
    glusterfsd_msg_20, glusterfsd_msg_21, glusterfsd_msg_22, glusterfsd_msg_23,
    glusterfsd_msg_24, glusterfsd_msg_25, glusterfsd_msg_26, glusterfsd_msg_27,
    glusterfsd_msg_28, glusterfsd_msg_29, glusterfsd_msg_30, glusterfsd_msg_31,
    glusterfsd_msg_32, glusterfsd_msg_33, glusterfsd_msg_34, glusterfsd_msg_35,
    glusterfsd_msg_36, glusterfsd_msg_37, glusterfsd_msg_38, glusterfsd_msg_39,
    glusterfsd_msg_40, glusterfsd_msg_41, glusterfsd_msg_42, glusterfsd_msg_43,
    glusterfsd_msg_029, glusterfsd_msg_041, glusterfsd_msg_042);

#define glusterfsd_msg_1_STR "Could not create absolute mountpoint path"
#define glusterfsd_msg_2_STR "Could not get current working directory"
#define glusterfsd_msg_4_STR "failed to set mount-point to options dictionary"
#define glusterfsd_msg_3_STR "failed to set dict value for key"
#define glusterfsd_msg_5_STR "failed to set disable for key"
#define glusterfsd_msg_6_STR "failed to set enable for key"
#define glusterfsd_msg_7_STR                                                   \
    "Not a client process, not performing mount operation"
#define glusterfsd_msg_8_STR "MOUNT_POINT initialization failed"
#define glusterfsd_msg_9_STR "loading volume file failed"
#define glusterfsd_msg_10_STR "xlator option is invalid"
#define glusterfsd_msg_11_STR "Fetching the volume file from server..."
#define glusterfsd_msg_12_STR "volume initialization failed"
#define glusterfsd_msg_34_STR "memory init failed"
#define glusterfsd_msg_13_STR "ERROR: glusterfs uuid generation failed"
#define glusterfsd_msg_14_STR "ERROR: glusterfs pool creation failed"
#define glusterfsd_msg_15_STR                                                  \
    "ERROR: '--volfile-id' is mandatory if '-s' OR '--volfile-server' option " \
    "is given"
#define glusterfsd_msg_16_STR "ERROR: parsing the volfile failed"
#define glusterfsd_msg_33_STR                                                  \
    "obsolete option '--volfile-max-fecth-attempts or fetch-attempts' was "    \
    "provided"
#define glusterfsd_msg_17_STR "pidfile open failed"
#define glusterfsd_msg_18_STR "pidfile lock failed"
#define glusterfsd_msg_20_STR "pidfile truncation failed"
#define glusterfsd_msg_21_STR "pidfile write failed"
#define glusterfsd_msg_22_STR "failed to exeute pthread_sigmask"
#define glusterfsd_msg_23_STR "failed to create pthread"
#define glusterfsd_msg_24_STR "daemonization failed"
#define glusterfsd_msg_25_STR "mount failed"
#define glusterfsd_msg_26_STR "failed to construct the graph"
#define glusterfsd_msg_27_STR "fuse xlator cannot be specified in volume file"
#define glusterfsd_msg_28_STR "Cannot reach volume specification file"
#define glusterfsd_msg_29_STR "ERROR: glusterfsd context not initialized"
#define glusterfsd_msg_43_STR                                                  \
    "command line argument --brick-mux is valid only for brick process"
#define glusterfsd_msg_029_STR "failed to create command line string"
#define glusterfsd_msg_30_STR "Started running version"
#define glusterfsd_msg_31_STR "Could not create new sync-environment"
#define glusterfsd_msg_40_STR "No change in volfile, countinuing"
#define glusterfsd_msg_39_STR "Unable to create/delete temporary file"
#define glusterfsd_msg_38_STR                                                  \
    "Not processing brick-op since volume graph is not yet active"
#define glusterfsd_msg_35_STR "rpc req buffer unserialization failed"
#define glusterfsd_msg_36_STR "problem in xlator loading"
#define glusterfsd_msg_37_STR "failed to get dict value"
#define glusterfsd_msg_41_STR "received attach request for volfile"
#define glusterfsd_msg_42_STR "failed to unserialize xdata to dictionary"
#define glusterfsd_msg_041_STR "can't detach. flie not found"
#define glusterfsd_msg_042_STR                                                 \
    "couldnot detach old graph. Aborting the reconfiguration operation"

#endif /* !_GLUSTERFSD_MESSAGES_H_ */
