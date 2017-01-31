/*
  Copyright (c) 2014 Red Hat, Inc. <http://www.redhat.com>
  This file is part of GlusterFS.

  This file is licensed to you under your choice of the GNU Lesser
  General Public License, version 3 or any later version (LGPLv3 or
  later), or the GNU General Public License, version 2 (GPLv2), in all
  cases as published by the Free Software Foundation.
*/

#ifndef _GLUSTERD_MESSAGES_H_
#define _GLUSTERD_MESSAGES_H_

#include "glfs-message-id.h"

/*! \file glusterd-messages.h
 *  \brief Glusterd log-message IDs and their descriptions
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
 * 3) Check across the code if the message ID macro in question is reused
 *    anywhere. If reused then then the modifications should ensure correctness
 *    everywhere, or needs a new message ID as (1) above was not adhered to. If
 *    not used anywhere, proceed with the required modification.
 * NOTE: Rules for message deletion
 * 4) Check (3) and if used anywhere else, then cannot be deleted. If not used
 *    anywhere, then can be deleted, but will leave a hole by design, as
 *    addition rules specify modification to the end of the list and not filling
 *    holes.
 */

#define GLUSTERD_COMP_BASE      GLFS_MSGID_GLUSTERD

#define GLFS_NUM_MESSAGES       597

#define GLFS_MSGID_END          (GLUSTERD_COMP_BASE + GLFS_NUM_MESSAGES + 1)
/* Messaged with message IDs */
#define glfs_msg_start_x GLFS_COMP_BASE, "Invalid: Start of messages"
/*------------*/

/*!
 * @messageid 106001
 * @diagnosis Operation could not be performed because the server quorum was not
 *            met
 * @recommendedaction Ensure that other peer nodes are online and reachable from
 *                    the local peer node
 */
#define GD_MSG_SERVER_QUORUM_NOT_MET (GLUSTERD_COMP_BASE + 1)

/*!
 * @messageid 106002
 * @diagnosis The local bricks belonging to the volume were killed because
 *            the server-quorum was not met
 * @recommendedaction Ensure that other peer nodes are online and reachable from
 *                    the local peer node
 */
#define GD_MSG_SERVER_QUORUM_LOST_STOPPING_BRICKS (GLUSTERD_COMP_BASE + 2)

/*!
 * @messageid 106003
 * @diagnosis The local bricks belonging to the named volume were (re)started
 *            because the server-quorum was met
 * @recommendedaction  None
 */
#define GD_MSG_SERVER_QUORUM_MET_STARTING_BRICKS (GLUSTERD_COMP_BASE + 3)

/*!
 * @messageid 106004
 * @diagnosis Glusterd on the peer might be down or unreachable
 * @recommendedaction  Check if glusterd is running on the peer node or if
 *                     the firewall rules are not blocking port 24007
 */
#define GD_MSG_PEER_DISCONNECTED (GLUSTERD_COMP_BASE + 4)

/*!
 * @messageid 106005
 * @diagnosis Brick process might be down
 * @recommendedaction Check brick log files to get more information on the cause
 *                    for the brick's offline status. To bring the brick back
 *                    online,run gluster volume start <VOLNAME> force
 */
#define GD_MSG_BRICK_DISCONNECTED (GLUSTERD_COMP_BASE + 5)

/*!
 * @messageid 106006
 * @diagnosis NFS Server or Self-heal daemon might be down
 * @recommendedaction Check nfs or self-heal daemon log files to get more
 *                    information on the cause for the brick's offline status.
 *                    To bring the brick back online, run gluster volume
 *                    start <VOLNAME> force
 */
#define GD_MSG_NODE_DISCONNECTED (GLUSTERD_COMP_BASE + 6)

/*!
 * @messageid 106007
 * @diagnosis Rebalance process might be down
 * @recommendedaction None
 */
#define GD_MSG_REBALANCE_DISCONNECTED (GLUSTERD_COMP_BASE + 7)

/*!
 * @messageid 106008
 * @diagnosis Volume cleanup failed
 * @recommendedaction None
 */
#define GD_MSG_VOL_CLEANUP_FAIL (GLUSTERD_COMP_BASE + 8)

/*!
 * @messageid 106009
 * @diagnosis Volume version mismatch while adding a peer
 * @recommendedaction None
 */
#define GD_MSG_VOL_VERS_MISMATCH (GLUSTERD_COMP_BASE + 9)

/*!
 * @messageid 106010
 * @diagnosis Volume checksum mismatch while adding a peer
 * @recommendedaction Check for which node the checksum mismatch happens
 *                    and delete the volume configuration files from it andi
 *                    restart glusterd
 */
#define GD_MSG_CKSUM_VERS_MISMATCH (GLUSTERD_COMP_BASE + 10)

/*!
 * @messageid 106011
 * @diagnosis A volume quota-conf version mismatch occurred while adding a peer
 * @recommendedaction None
 */
#define GD_MSG_QUOTA_CONFIG_VERS_MISMATCH (GLUSTERD_COMP_BASE + 11)

/*!
 * @messageid 106012
 * @diagnosis A quota-conf checksum mismatch occurred while adding a peer
 * @recommendedaction Check for which node the checksum mismatch happens
 *                    and delete the volume configuration files from it and
 *                    restart glusterd
 */
#define GD_MSG_QUOTA_CONFIG_CKSUM_MISMATCH (GLUSTERD_COMP_BASE + 12)

/*!
 * @messageid 106013
 * @diagnosis Brick process could not be terminated
 * @recommendedaction Find the pid of the brick process from the log file and
 *                    manually kill it
 */
#define GD_MSG_BRICK_STOP_FAIL (GLUSTERD_COMP_BASE + 13)

/*!
 * @messageid 106014
 * @diagnosis One of the listed services:NFS Server, Quota Daemon, Self Heal
 *            Daemon, or brick process could not be brought offline
 * @recommendedaction Find the pid of the process from the log file and
 *                    manually kill it
 */
#define GD_MSG_SVC_KILL_FAIL (GLUSTERD_COMP_BASE + 14)

/*!
 * @messageid 106015
 * @diagnosis The process could not be killed with the specified PID
 * @recommendedaction None
 */
#define GD_MSG_PID_KILL_FAIL (GLUSTERD_COMP_BASE + 15)

/*!
 * @messageid 106016
 * @diagnosis Rebalance socket file is not found
 * @recommendedaction Rebalance failed as the socket file for rebalance is
 *                    missing. Restart the rebalance process
 */
#define GD_MSG_REBAL_NO_SOCK_FILE (GLUSTERD_COMP_BASE + 16)

/*!
 * @messageid 106017
 * @diagnosis Unix options could not be set
 * @recommendedaction Server is out of memory and needs a restart
 */
#define GD_MSG_UNIX_OP_BUILD_FAIL (GLUSTERD_COMP_BASE + 17)

/*!
 * @messageid 106018
 * @diagnosis RPC creation failed
 * @recommendedaction Rebalance failed as glusterd could not establish an RPC
 *                    connection. Check the log file for the exact reason of the
 *                    failure and then restart the rebalance process
 */
#define GD_MSG_RPC_CREATE_FAIL (GLUSTERD_COMP_BASE + 18)

/*!
 * @messageid 106019
 * @diagnosis The default options on volume could not be set with the volume
 *            create and volume reset commands
 * @recommendedaction Check glusterd log files to see the exact reason for
 *                    failure to set default options
 */
#define GD_MSG_FAIL_DEFAULT_OPT_SET (GLUSTERD_COMP_BASE + 19)

/*!
 * @messageid 106020
 * @diagnosis Failed to release cluster wide lock for one of the peer
 * @recommendedaction Restart the glusterd service on the node where the command
 * was issued
 */
#define GD_MSG_CLUSTER_UNLOCK_FAILED (GLUSTERD_COMP_BASE + 20)

/*!
 * @messageid 106021
 * @diagnosis
 * @recommendedaction
 *
 */
#define GD_MSG_NO_MEMORY    (GLUSTERD_COMP_BASE + 21)

/*!
 * @messageid 106022
 * @diagnosis
 * @recommendedaction
 *
 */
#define GD_MSG_UNSUPPORTED_VERSION    (GLUSTERD_COMP_BASE + 22)

/*!
 * @messageid 106023
 * @diagnosis
 * @recommendedaction
 *
 */
#define GD_MSG_COMMAND_NOT_FOUND    (GLUSTERD_COMP_BASE + 23)

/*!
 * @messageid 106024
 * @diagnosis
 * @recommendedaction
 *
 */
#define GD_MSG_SNAPSHOT_OP_FAILED    (GLUSTERD_COMP_BASE + 24)

/*!
 * @messageid 106025
 * @diagnosis
 * @recommendedaction
 *
 */
#define GD_MSG_INVALID_ENTRY    (GLUSTERD_COMP_BASE + 25)

/*!
 * @messageid
 * @diagnosis
 * @recommendedaction
 *
 */
#define GD_MSG_VOL_NOT_FOUND    (GLUSTERD_COMP_BASE + 27)

/*!
 * @messageid
 * @diagnosis
 * @recommendedaction
 *
 */
#define GD_MSG_REG_COMPILE_FAILED    (GLUSTERD_COMP_BASE + 28)

/*!
 * @messageid
 * @diagnosis
 * @recommendedaction
 *
 */
#define GD_MSG_FILE_OP_FAILED             (GLUSTERD_COMP_BASE + 29)

/*!
 * @messageid
 * @diagnosis
 * @recommendedaction
 *
 */
#define GD_MSG_SNAP_CREATION_FAIL        (GLUSTERD_COMP_BASE + 30)

/*!
 * @messageid
 * @diagnosis
 * @recommendedaction
 *
 */
#define GD_MSG_VOL_OP_FAILED             (GLUSTERD_COMP_BASE + 31)

/*!
 * @messageid
 * @diagnosis
 * @recommendedaction
 *
 */
#define GD_MSG_CREATE_DIR_FAILED        (GLUSTERD_COMP_BASE + 32)

/*!
 * @messageid
 * @diagnosis
 * @recommendedaction
 *
 */
#define GD_MSG_DIR_OP_FAILED            (GLUSTERD_COMP_BASE + 33)

/*!
 * @messageid
 * @diagnosis
 * @recommendedaction
 *
 */
#define GD_MSG_VOL_STOP_FAILED          (GLUSTERD_COMP_BASE + 34)

/*!
 * @messageid
 * @diagnosis
 * @recommendedaction
 *
 */
#define GD_MSG_NO_CLI_RESP                (GLUSTERD_COMP_BASE + 35)


/*!
 * @messageid
 * @diagnosis
 * @recommendedaction
 *
 */
#define GD_MSG_LOCK_INIT_FAILED          (GLUSTERD_COMP_BASE + 36)


/*!
 * @messageid
 * @diagnosis
 * @recommendedaction
 *
 */
#define GD_MSG_SNAP_LIST_GET_FAIL        (GLUSTERD_COMP_BASE + 37)


/*!
 * @messageid
 * @diagnosis
 * @recommendedaction
 *
 */
#define GD_MSG_UNOUNT_FAILED              (GLUSTERD_COMP_BASE + 38)

/*!
 * @messageid
 * @diagnosis
 * @recommendedaction
 *
 */
#define GD_MSG_LOCK_DESTROY_FAILED       (GLUSTERD_COMP_BASE + 39)

/*!
 * @messageid
 * @diagnosis
 * @recommendedaction
 *
 */
#define GD_MSG_SNAP_CLEANUP_FAIL         (GLUSTERD_COMP_BASE + 40)

/*!
 * @messageid
 * @diagnosis
 * @recommendedaction
 *
 */
#define GD_MSG_SNAP_ACTIVATE_FAIL        (GLUSTERD_COMP_BASE + 41)

/*!
 * @messageid
 * @diagnosis
 * @recommendedaction
 *
 */
#define GD_MSG_SNAP_DEACTIVATE_FAIL     (GLUSTERD_COMP_BASE + 42)

/*!
 * @messageid
 * @diagnosis
 * @recommendedaction
 *
 */
#define GD_MSG_SNAP_RESTORE_FAIL        (GLUSTERD_COMP_BASE + 43)

/*!
 * @messageid
 * @diagnosis
 * @recommendedaction
 *
 */
#define GD_MSG_SNAP_REMOVE_FAIL         (GLUSTERD_COMP_BASE + 44)

/*!
 * @messageid
 * @diagnosis
 * @recommendedaction
 *
 */
#define GD_MSG_SNAP_CONFIG_FAIL         (GLUSTERD_COMP_BASE + 45)

/*!
 * @messageid
 * @diagnosis
 * @recommendedaction
 *
 */
#define GD_MSG_SNAP_STATUS_FAIL         (GLUSTERD_COMP_BASE + 46)

/*!
 * @messageid
 * @diagnosis
 * @recommendedaction
 *
 */
#define GD_MSG_SNAP_INIT_FAIL           (GLUSTERD_COMP_BASE + 47)

/*!
 * @messageid
 * @diagnosis
 * @recommendedaction
 *
 */
#define GD_MSG_VOLINFO_SET_FAIL         (GLUSTERD_COMP_BASE + 48)

/*!
 * @messageid
 * @diagnosis
 * @recommendedaction
 *
 */
#define GD_MSG_VOLINFO_GET_FAIL         (GLUSTERD_COMP_BASE + 49)

/*!
 * @messageid
 * @diagnosis
 * @recommendedaction
 *
 */
#define GD_MSG_BRICK_CREATION_FAIL      (GLUSTERD_COMP_BASE + 50)

/*!
 * @messageid
 * @diagnosis
 * @recommendedaction
 *
 */
#define GD_MSG_BRICK_GET_INFO_FAIL      (GLUSTERD_COMP_BASE + 51)

/*!
 * @messageid
 * @diagnosis
 * @recommendedaction
 *
 */
#define GD_MSG_BRICK_NEW_INFO_FAIL      (GLUSTERD_COMP_BASE + 52)

/*!
 * @messageid
 * @diagnosis
 * @recommendedaction
 *
 */
#define GD_MSG_LVS_FAIL                 (GLUSTERD_COMP_BASE + 53)

/*!
 * @messageid
 * @diagnosis
 * @recommendedaction
 *
 */
#define GD_MSG_SETXATTR_FAIL            (GLUSTERD_COMP_BASE + 54)

/*!
 * @messageid
 * @diagnosis
 * @recommendedaction
 *
 */
#define GD_MSG_UMOUNTING_SNAP_BRICK     (GLUSTERD_COMP_BASE + 55)

/*!
 * @messageid
 * @diagnosis
 * @recommendedaction
 *
 */
#define GD_MSG_OP_UNSUPPORTED           (GLUSTERD_COMP_BASE + 56)

/*!
 * @messageid
 * @diagnosis
 * @recommendedaction
 *
 */
#define GD_MSG_SNAP_NOT_FOUND           (GLUSTERD_COMP_BASE + 57)

/*!
 * @messageid
 * @diagnosis
 * @recommendedaction
 *
 */
#define GD_MSG_FS_LABEL_UPDATE_FAIL     (GLUSTERD_COMP_BASE + 58)

/*!
 * @messageid
 * @diagnosis
 * @recommendedaction
 *
 */
#define GD_MSG_LVM_MOUNT_FAILED         (GLUSTERD_COMP_BASE + 59)

/*!
 * @messageid
 * @diagnosis
 * @recommendedaction
 *
 */
#define GD_MSG_DICT_SET_FAILED          (GLUSTERD_COMP_BASE + 60)

/*!
 * @messageid
 * @diagnosis
 * @recommendedaction
 *
 */
#define GD_MSG_CANONICALIZE_FAIL        (GLUSTERD_COMP_BASE + 61)

/*!
 * @messageid
 * @diagnosis
 * @recommendedaction
 *
 */
#define GD_MSG_DICT_GET_FAILED          (GLUSTERD_COMP_BASE + 62)

/*!
 * @messageid
 * @diagnosis
 * @recommendedaction
 *
 */
#define GD_MSG_SNAP_INFO_FAIL   (GLUSTERD_COMP_BASE + 63)

/*!
 * @messageid
 * @diagnosis
 * @recommendedaction
 *
 */
#define GD_MSG_SNAP_VOL_CONFIG_FAIL     (GLUSTERD_COMP_BASE + 64)

/*!
 * @messageid
 * @diagnosis
 * @recommendedaction
 *
 */
#define GD_MSG_SNAP_OBJECT_STORE_FAIL   (GLUSTERD_COMP_BASE + 65)

/*!
 * @messageid
 * @diagnosis
 * @recommendedaction
 *
 */
#define GD_MSG_DICT_UNSERIALIZE_FAIL    (GLUSTERD_COMP_BASE + 66)

/*!
 * @messageid
 * @diagnosis
 * @recommendedaction
 *
 */
#define GD_MSG_SNAP_RESTORE_REVERT_FAIL        (GLUSTERD_COMP_BASE + 67)

/*!
 * @messageid
 * @diagnosis
 * @recommendedaction
 *
 */
#define GD_MSG_SNAP_LIST_SET_FAIL       (GLUSTERD_COMP_BASE + 68)

/*!
 * @messageid
 * @diagnosis
 * @recommendedaction
 *
 */
#define GD_MSG_VOLFILE_CREATE_FAIL      (GLUSTERD_COMP_BASE + 69)

/*!
 * @messageid
 * @diagnosis
 * @recommendedaction
 *
 */
#define GD_MSG_VOLINFO_REMOVE_FAIL      (GLUSTERD_COMP_BASE + 70)

/*!
 * @messageid
 * @diagnosis
 * @recommendedaction
 *
 */
#define GD_MSG_VOL_DELETE_FAIL          (GLUSTERD_COMP_BASE + 71)

/*!
 * @messageid
 * @diagnosis
 * @recommendedaction
 *
 */
#define GD_MSG_SNAPSHOT_PENDING         (GLUSTERD_COMP_BASE + 72)

/*!
 * @messageid
 * @diagnosis
 * @recommendedaction
 *
 */
#define GD_MSG_BRICK_PATH_UNMOUNTED         (GLUSTERD_COMP_BASE + 73)

/*!
 * @messageid
 * @diagnosis
 * @recommendedaction
 *
 */
#define GD_MSG_BRICK_ADD_FAIL           (GLUSTERD_COMP_BASE + 74)

/*!
 * @messageid
 * @diagnosis
 * @recommendedaction
 *
 */
#define GD_MSG_BRICK_SET_INFO_FAIL      (GLUSTERD_COMP_BASE + 75)

/*!
 * @messageid
 * @diagnosis
 * @recommendedaction
 *
 */
#define GD_MSG_LVCREATE_FAIL            (GLUSTERD_COMP_BASE + 76)

/*!
 * @messageid
 * @diagnosis
 * @recommendedaction
 *
 */
#define GD_MSG_VG_GET_FAIL      (GLUSTERD_COMP_BASE + 77)

/*!
 * @messageid
 * @diagnosis
 * @recommendedaction
 *
 */
#define GD_MSG_TPOOL_GET_FAIL          (GLUSTERD_COMP_BASE + 78)

/*!
 * @messageid
 * @diagnosis
 * @recommendedaction
 *
 */
#define GD_MSG_LVM_REMOVE_FAILED        (GLUSTERD_COMP_BASE + 79)

/*!
 * @messageid
 * @diagnosis
 * @recommendedaction
 *
 */
#define GD_MSG_MISSEDSNAP_INFO_SET_FAIL         (GLUSTERD_COMP_BASE + 80)

/*!
 * @messageid
 * @diagnosis
 * @recommendedaction
 *
 */
#define GD_MSG_BRK_MOUNTOPTS_FAIL      (GLUSTERD_COMP_BASE + 81)

/*!
 * @messageid
 * @diagnosis
 * @recommendedaction
 *
 */
#define GD_MSG_MISSED_SNAP_LIST_STORE_FAIL        (GLUSTERD_COMP_BASE + 82)

/*!
 * @messageid
 * @diagnosis
 * @recommendedaction
 *
 */
#define GD_MSG_INVALID_MISSED_SNAP_ENTRY        (GLUSTERD_COMP_BASE + 83)

/*!
 * @messageid
 * @diagnosis
 * @recommendedaction
 *
 */
#define GD_MSG_MISSED_SNAP_GET_FAIL         (GLUSTERD_COMP_BASE + 84)

/*!
 * @messageid
 * @diagnosis
 * @recommendedaction
 *
 */
#define GD_MSG_MISSED_SNAP_CREATE_FAIL          (GLUSTERD_COMP_BASE + 85)

/*!
 * @messageid
 * @diagnosis
 * @recommendedaction
 *
 */
#define GD_MSG_DUP_ENTRY         (GLUSTERD_COMP_BASE + 86)

/*!
 * @messageid
 * @diagnosis
 * @recommendedaction
 *
 */
#define GD_MSG_MISSED_SNAP_STATUS_DONE          (GLUSTERD_COMP_BASE + 87)

/*!
 * @messageid
 * @diagnosis
 * @recommendedaction
 *
 */
#define GD_MSG_NO_EXEC_PERMS            (GLUSTERD_COMP_BASE + 88)

/*!
 * @messageid
 * @diagnosis
 * @recommendedaction
 *
 */
#define GD_MSG_GLOBAL_OP_VERSION_SET_FAIL       (GLUSTERD_COMP_BASE + 89)

/*!
 * @messageid
 * @diagnosis
 * @recommendedaction
 *
 */
#define GD_MSG_HARD_LIMIT_SET_FAIL      (GLUSTERD_COMP_BASE + 90)

/*!
 * @messageid
 * @diagnosis
 * @recommendedaction
 *
 */
#define GD_MSG_OP_SUCCESS      (GLUSTERD_COMP_BASE + 91)

/*!
 * @messageid
 * @diagnosis
 * @recommendedaction
 *
 */
#define GD_MSG_STORE_FAIL       (GLUSTERD_COMP_BASE + 92)

/*!
 * @messageid
 * @diagnosis
 * @recommendedaction
 *
 */
#define GD_MSG_GLOBAL_OP_VERSION_GET_FAIL       (GLUSTERD_COMP_BASE + 93)

/*!
 * @messageid
 * @diagnosis
 * @recommendedaction
 *
 */
#define GD_MSG_GEOREP_GET_FAILED        (GLUSTERD_COMP_BASE + 94)

/*!
 * @messageid
 * @diagnosis
 * @recommendedaction
 *
 */
#define GD_MSG_GLUSTERD_UMOUNT_FAIL     (GLUSTERD_COMP_BASE + 95)

/*!
 * @messageid
 * @diagnosis
 * @recommendedaction
 *
 */
#define GD_MSG_QUORUM_CHECK_FAIL        (GLUSTERD_COMP_BASE + 96)

/*!
 * @messageid
 * @diagnosis
 * @recommendedaction
 *
 */
#define GD_MSG_QUORUM_COUNT_IGNORED     (GLUSTERD_COMP_BASE + 97)

/*!
 * @messageid
 * @diagnosis
 * @recommendedaction
 *
 */
#define GD_MSG_SNAP_MOUNT_FAIL          (GLUSTERD_COMP_BASE + 98)

/*!
 * @messageid
 * @diagnosis
 * @recommendedaction
 *
 */
#define GD_MSG_RSP_DICT_USE_FAIL       (GLUSTERD_COMP_BASE + 99)

/*!
 * @messageid
 * @diagnosis
 * @recommendedaction
 *
 */
#define GD_MSG_SNAP_IMPORT_FAIL         (GLUSTERD_COMP_BASE + 100)

/*!
 * @messageid
 * @diagnosis
 * @recommendedaction
 *
 */
#define GD_MSG_SNAP_CONFLICT            (GLUSTERD_COMP_BASE + 101)

/*!
 * @messageid
 * @diagnosis
 * @recommendedaction
 *
 */
#define GD_MSG_MISSED_SNAP_DELETE       (GLUSTERD_COMP_BASE + 102)

/*!
 * @messageid
 * @diagnosis
 * @recommendedaction
 *
 */
#define GD_MSG_QUOTA_CONFIG_IMPORT_FAIL         (GLUSTERD_COMP_BASE + 103)

/*!
 * @messageid
 * @diagnosis
 * @recommendedaction
 *
 */
#define GD_MSG_SNAPDIR_CREATE_FAIL      (GLUSTERD_COMP_BASE + 104)

/*!
 * @messageid
 * @diagnosis
 * @recommendedaction
 *
 */
#define GD_MSG_MISSED_SNAP_PRESENT      (GLUSTERD_COMP_BASE + 105)

/*!
 * @messageid
 * @diagnosis
 * @recommendedaction
 *
 */
#define GD_MSG_UUID_NULL                (GLUSTERD_COMP_BASE + 106)

/*!
 * @messageid
 * @diagnosis
 * @recommendedaction
 *
 */
#define GD_MSG_TSTAMP_SET_FAIL          (GLUSTERD_COMP_BASE + 107)

/*!
 * @messageid
 * @diagnosis
 * @recommendedaction
 *
 */
#define GD_MSG_RESP_AGGR_FAIL           (GLUSTERD_COMP_BASE + 108)

/*!
 * @messageid
 * @diagnosis
 * @recommendedaction
 *
 */
#define GD_MSG_DICT_EMPTY               (GLUSTERD_COMP_BASE + 109)

/*!
 * @messageid
 * @diagnosis
 * @recommendedaction
 *
 */
#define GD_MSG_DICT_CREATE_FAIL         (GLUSTERD_COMP_BASE + 110)

/*!
 * @messageid
 * @diagnosis
 * @recommendedaction
 *
 */
#define GD_MSG_SNAPD_STOP_FAIL          (GLUSTERD_COMP_BASE + 111)

/*!
 * @messageid
 * @diagnosis
 * @recommendedaction
 *
 */
#define GD_MSG_SOFT_LIMIT_REACHED       (GLUSTERD_COMP_BASE + 112)

/*!
 * @messageid
 * @diagnosis
 * @recommendedaction
 *
 */
#define GD_MSG_SNAPD_START_FAIL        (GLUSTERD_COMP_BASE + 113)

/*!
 * @messageid
 * @diagnosis
 * @recommendedaction
 *
 */
#define GD_MSG_SNAPD_CREATE_FAIL        (GLUSTERD_COMP_BASE + 114)

/*!
 * @messageid
 * @diagnosis
 * @recommendedaction
 *
 */
#define GD_MSG_SNAPD_INIT_FAIL          (GLUSTERD_COMP_BASE + 115)

/*!
 * @messageid
 * @diagnosis
 * @recommendedaction
 *
 */
#define GD_MSG_MGMTV3_OP_FAIL           (GLUSTERD_COMP_BASE + 116)

/*!
 * @messageid
 * @diagnosis
 * @recommendedaction
 *
 */
#define GD_MSG_MGMTV3_PAYLOAD_BUILD_FAIL        (GLUSTERD_COMP_BASE + 117)

/*!
 * @messageid
 * @diagnosis
 * @recommendedaction
 *
 */
#define GD_MSG_MGMTV3_UNLOCK_FAIL   (GLUSTERD_COMP_BASE + 118)

/*!
 * @messageid
 * @diagnosis
 * @recommendedaction
 *
 */
#define GD_MSG_MGMTV3_LOCK_GET_FAIL     (GLUSTERD_COMP_BASE + 119)

/*!
 * @messageid
 * @diagnosis
 * @recommendedaction
 *
 */
#define GD_MSG_MGMTV3_LOCKDOWN_FAIL     (GLUSTERD_COMP_BASE + 120)

/*!
 * @messageid
 * @diagnosis
 * @recommendedaction
 *
 */
#define GD_MSG_POST_VALIDATION_FAIL     (GLUSTERD_COMP_BASE + 121)

/*!
 * @messageid
 * @diagnosis
 * @recommendedaction
 *
 */
#define GD_MSG_PRE_VALIDATION_FAIL      (GLUSTERD_COMP_BASE + 122)

/*!
 * @messageid
 * @diagnosis
 * @recommendedaction
 *
 */
#define GD_MSG_COMMIT_OP_FAIL           (GLUSTERD_COMP_BASE + 123)

/*!
 * @messageid
 * @diagnosis
 * @recommendedaction
 *
 */
#define GD_MSG_PEER_LIST_CREATE_FAIL    (GLUSTERD_COMP_BASE + 124)

/*!
 * @messageid
 * @diagnosis
 * @recommendedaction
 *
 */
#define GD_MSG_BRICK_OP_FAIL            (GLUSTERD_COMP_BASE + 125)

/*!
 * @messageid
 * @diagnosis
 * @recommendedaction
 *
 */
#define GD_MSG_OPINFO_SET_FAIL          (GLUSTERD_COMP_BASE + 126)

/*!
 * @messageid
 * @diagnosis
 * @recommendedaction
 *
 */
#define GD_MSG_OP_EVENT_UNLOCK_FAIL    (GLUSTERD_COMP_BASE + 127)

/*!
 * @messageid
 * @diagnosis
 * @recommendedaction
 *
 */
#define GD_MSG_MGMTV3_OP_RESP_FAIL      (GLUSTERD_COMP_BASE + 128)

/*!
 * @messageid
 * @diagnosis
 * @recommendedaction
 *
 */
#define GD_MSG_PEER_NOT_FOUND           (GLUSTERD_COMP_BASE + 129)

/*!
 * @messageid
 * @diagnosis
 * @recommendedaction
 *
 */
#define GD_MSG_REQ_DECODE_FAIL          (GLUSTERD_COMP_BASE + 130)

/*!
 * @messageid
 * @diagnosis
 * @recommendedaction
 *
 */
#define GD_MSG_DICT_SERL_LENGTH_GET_FAIL        (GLUSTERD_COMP_BASE + 131)

/*!
 * @messageid
 * @diagnosis
 * @recommendedaction
 *
 */
#define GD_MSG_ALREADY_STOPPED          (GLUSTERD_COMP_BASE + 132)

/*!
 * @messageid
 * @diagnosis
 * @recommendedaction
 *
 */
#define GD_MSG_PRE_VALD_RESP_FAIL       (GLUSTERD_COMP_BASE + 133)

/*!
 * @messageid
 * @diagnosis
 * @recommendedaction
 *
 */
#define GD_MSG_SVC_GET_FAIL             (GLUSTERD_COMP_BASE + 134)

/*!
 * @messageid
 * @diagnosis
 * @recommendedaction
 *
 */
#define GD_MSG_VOLFILE_NOT_FOUND        (GLUSTERD_COMP_BASE + 135)

/*!
 * @messageid
 * @diagnosis
 * @recommendedaction
 *
 */
#define GD_MSG_OP_EVENT_LOCK_FAIL    (GLUSTERD_COMP_BASE + 136)

/*!
 * @messageid
 * @diagnosis
 * @recommendedaction
 *
 */
#define GD_MSG_NON_STRIPE_VOL           (GLUSTERD_COMP_BASE + 137)

/*!
 * @messageid
 * @diagnosis
 * @recommendedaction
 *
 */
#define GD_MSG_SNAPD_OBJ_GET_FAIL      (GLUSTERD_COMP_BASE + 138)

/*!
 * @messageid
 * @diagnosis
 * @recommendedaction
 *
 */
#define GD_MSG_QUOTA_DISABLED           (GLUSTERD_COMP_BASE + 139)

/*!
 * @messageid
 * @diagnosis
 * @recommendedaction
 *
 */
#define GD_MSG_CACHE_MINMAX_SIZE_INVALID    (GLUSTERD_COMP_BASE + 140)

/*!
 * @messageid
 * @diagnosis
 * @recommendedaction
 *
 */
#define GD_MSG_QUOTA_GET_STAT_FAIL          (GLUSTERD_COMP_BASE + 141)

/*!
 * @messageid
 * @diagnosis
 * @recommendedaction
 *
 */
#define GD_MSG_SUBVOLUMES_EXCEED          (GLUSTERD_COMP_BASE + 142)

/*!
 * @messageid
 * @diagnosis
 * @recommendedaction
 *
 */
#define GD_MSG_BRICK_ADD                 (GLUSTERD_COMP_BASE + 143)

/*!
 * @messageid
 * @diagnosis
 * @recommendedaction
 *
 */
#define GD_MSG_BRICK_REMOVE                 (GLUSTERD_COMP_BASE + 144)

/*!
 * @messageid
 * @diagnosis
 * @recommendedaction
 *
 */
#define GD_MSG_CREATE_KEY_FAIL          (GLUSTERD_COMP_BASE + 145)

/*!
 * @messageid
 * @diagnosis
 * @recommendedaction
 *
 */
#define GD_MSG_MULTIPLE_LOCK_ACQUIRE_FAIL       (GLUSTERD_COMP_BASE + 146)

/*!
 * @messageid
 * @diagnosis
 * @recommendedaction
 *
 */
#define GD_MSG_MULTIPLE_LOCK_RELEASE_FAIL       (GLUSTERD_COMP_BASE + 147)

/*!
 * @messageid
 * @diagnosis
 * @recommendedaction
 *
 */
#define GD_MSG_RESP_FROM_UNKNOWN_PEER           (GLUSTERD_COMP_BASE + 148)

/*!
 * @messageid
 * @diagnosis
 * @recommendedaction
 *
 */
#define GD_MSG_BRICK_MOUNDIRS_AGGR_FAIL         (GLUSTERD_COMP_BASE + 149)

/*!
 * @messageid
 * @diagnosis
 * @recommendedaction
 *
 */
#define GD_MSG_GFID_VALIDATE_SET_FAIL           (GLUSTERD_COMP_BASE + 150)

/*!
 * @messageid
 * @diagnosis
 * @recommendedaction
 *
 */
#define GD_MSG_PEER_LOCK_FAIL                   (GLUSTERD_COMP_BASE + 151)

/*!
 * @messageid
 * @diagnosis
 * @recommendedaction
 *
 */
#define GD_MSG_PEER_UNLOCK_FAIL                 (GLUSTERD_COMP_BASE + 152)

/*!
 * @messageid
 * @diagnosis
 * @recommendedaction
 *
 */
#define GD_MSG_MGMT_OP_FAIL                     (GLUSTERD_COMP_BASE + 153)

/*!
 * @messageid
 * @diagnosis
 * @recommendedaction
 *
 */
#define GD_MSG_TRANS_OPINFO_CLEAR_FAIL          (GLUSTERD_COMP_BASE + 154)

/*!
 * @messageid
 * @diagnosis
 * @recommendedaction
 *
 */
#define GD_MSG_GLUSTERD_LOCK_FAIL               (GLUSTERD_COMP_BASE + 155)

/*!
 * @messageid
 * @diagnosis
 * @recommendedaction
 *
 */
#define GD_MSG_TRANS_OPINFO_SET_FAIL            (GLUSTERD_COMP_BASE + 156)

/*!
 * @messageid
 * @diagnosis
 * @recommendedaction
 *
 */
#define GD_MSG_TRANS_IDGEN_FAIL                 (GLUSTERD_COMP_BASE + 157)

/*!
 * @messageid
 * @diagnosis
 * @recommendedaction
 *
 */
#define GD_MSG_RPC_FAILURE                      (GLUSTERD_COMP_BASE + 158)

/*!
 * @messageid
 * @diagnosis
 * @recommendedaction
 *
 */
#define GD_MSG_OP_VERS_ADJUST_FAIL              (GLUSTERD_COMP_BASE + 159)

/*!
 * @messageid
 * @diagnosis
 * @recommendedaction
 *
 */
#define GD_MSG_SNAP_DEVICE_NAME_GET_FAIL        (GLUSTERD_COMP_BASE + 160)

/*!
 * @messageid
 * @diagnosis
 * @recommendedaction
 *
 */
#define GD_MSG_SNAP_STATUS_NOT_PENDING          (GLUSTERD_COMP_BASE + 161)

/*!
 * @messageid
 * @diagnosis
 * @recommendedaction
 *
 */
#define GD_MSG_MGMT_PGM_SET_FAIL                (GLUSTERD_COMP_BASE + 161)

/*!
 * @messageid
 * @diagnosis
 * @recommendedaction
 *
 */
#define GD_MSG_EVENT_INJECT_FAIL                (GLUSTERD_COMP_BASE + 162)

/*!
 * @messageid
 * @diagnosis
 * @recommendedaction
 *
 */
#define GD_MSG_VERS_INFO                        (GLUSTERD_COMP_BASE + 163)

/*!
 * @messageid
 * @diagnosis
 * @recommendedaction
 *
 */
#define GD_MSG_VOL_INFO_REQ_RECVD               (GLUSTERD_COMP_BASE + 164)

/*!
 * @messageid
 * @diagnosis
 * @recommendedaction
 *
 */
#define GD_MSG_VERS_GET_FAIL                    (GLUSTERD_COMP_BASE + 165)

/*!
 * @messageid
 * @diagnosis
 * @recommendedaction
 *
 */
#define GD_MSG_EVENT_NEW_GET_FAIL               (GLUSTERD_COMP_BASE + 166)

/*!
 * @messageid
 * @diagnosis
 * @recommendedaction
 *
 */
#define GD_MSG_RPC_LAYER_ERROR                  (GLUSTERD_COMP_BASE + 167)

/*!
 * @messageid
 * @diagnosis
 * @recommendedaction
 *
 */
#define GD_MSG_NO_HANDSHAKE_ACK                 (GLUSTERD_COMP_BASE + 168)

/*!
 * @messageid
 * @diagnosis
 * @recommendedaction
 *
 */
#define GD_MSG_OP_VERSION_MISMATCH              (GLUSTERD_COMP_BASE + 169)

/*!
 * @messageid
 * @diagnosis
 * @recommendedaction
 *
 */
#define GD_MSG_HANDSHAKE_REQ_REJECTED           (GLUSTERD_COMP_BASE + 170)

/*!
 * @messageid
 * @diagnosis
 * @recommendedaction
 *
 */
#define GD_MSG_UNKNOWN_MODE                     (GLUSTERD_COMP_BASE + 171)

/*!
 * @messageid
 * @diagnosis
 * @recommendedaction
 *
 */
#define GD_MSG_DEFRAG_STATUS_UPDATED            (GLUSTERD_COMP_BASE + 172)

/*!
 * @messageid
 * @diagnosis
 * @recommendedaction
 *
 */
#define GD_MSG_NO_FLAG_SET                      (GLUSTERD_COMP_BASE + 173)

/*!
 * @messageid
 * @diagnosis
 * @recommendedaction
 *
 */
#define GD_MSG_VERSION_UNSUPPORTED              (GLUSTERD_COMP_BASE + 174)

/*!
 * @messageid
 * @diagnosis
 * @recommendedaction
 *
 */
#define GD_MSG_UUID_SET_FAIL                    (GLUSTERD_COMP_BASE + 175)

/*!
 * @messageid
 * @diagnosis
 * @recommendedaction
 *
 */
#define GD_MSG_MOUNT_REQ_FAIL                    (GLUSTERD_COMP_BASE + 176)

/*!
 * @messageid
 * @diagnosis
 * @recommendedaction
 *
 */
#define GD_MSG_GLUSTERD_GLOBAL_INFO_STORE_FAIL   (GLUSTERD_COMP_BASE + 177)

/*!
 * @messageid
 * @diagnosis
 * @recommendedaction
 *
 */
#define GD_MSG_OP_VERS_STORE_FAIL               (GLUSTERD_COMP_BASE + 178)

/*!
 * @messageid
 * @diagnosis
 * @recommendedaction
 *
 */
#define GD_MSG_SNAP_AUTOMIC_UPDATE_FAIL         (GLUSTERD_COMP_BASE + 179)

/*!
 * @messageid
 * @diagnosis
 * @recommendedaction
 *
 */
#define GD_MSG_SNAPINFO_WRITE_FAIL              (GLUSTERD_COMP_BASE + 180)

/*!
 * @messageid
 * @diagnosis
 * @recommendedaction
 *
 */
#define GD_MSG_SNAPINFO_CREATE_FAIL             (GLUSTERD_COMP_BASE + 181)

/*!
 * @messageid
 * @diagnosis
 * @recommendedaction
 *
 */
#define GD_MSG_SNAPD_INFO_STORE_FAIL            (GLUSTERD_COMP_BASE + 182)

/*!
 * @messageid
 * @diagnosis
 * @recommendedaction
 *
 */
#define GD_MSG_BRK_MNTPATH_MOUNT_FAIL           (GLUSTERD_COMP_BASE + 183)

/*!
 * @messageid
 * @diagnosis
 * @recommendedaction
 *
 */
#define GD_MSG_BRK_MNTPATH_GET_FAIL             (GLUSTERD_COMP_BASE + 184)

/*!
 * @messageid
 * @diagnosis
 * @recommendedaction
 *
 */
#define GD_MSG_SNAP_BRK_MNT_RECREATE_FAIL       (GLUSTERD_COMP_BASE + 185)

/*!
 * @messageid
 * @diagnosis
 * @recommendedaction
 *
 */
#define GD_MSG_SNAP_RESOLVE_BRICK_FAIL          (GLUSTERD_COMP_BASE + 186)

/*!
 * @messageid
 * @diagnosis
 * @recommendedaction
 *
 */
#define GD_MSG_RESOLVE_BRICK_FAIL               (GLUSTERD_COMP_BASE + 187)

/*!
 * @messageid
 * @diagnosis
 * @recommendedaction
 *
 */
#define GD_MSG_BRK_MNT_RECREATE_FAIL            (GLUSTERD_COMP_BASE + 188)

/*!
 * @messageid
 * @diagnosis
 * @recommendedaction
 *
 */
#define GD_MSG_TMP_FILE_UNLINK_FAIL             (GLUSTERD_COMP_BASE + 189)

/*!
 * @messageid
 * @diagnosis
 * @recommendedaction
 *
 */
#define GD_MSG_VOL_VALS_WRITE_FAIL              (GLUSTERD_COMP_BASE + 190)

/*!
 * @messageid
 * @diagnosis
 * @recommendedaction
 *
 */
#define GD_MSG_STORE_HANDLE_GET_FAIL            (GLUSTERD_COMP_BASE + 191)

/*!
 * @messageid
 * @diagnosis
 * @recommendedaction
 *
 */
#define GD_MSG_STORE_HANDLE_WRITE_FAIL          (GLUSTERD_COMP_BASE + 192)

/*!
 * @messageid
 * @diagnosis
 * @recommendedaction
 *
 */
#define GD_MSG_MISSED_SNAP_LIST_STORE_HANDLE_GET_FAIL    \
                                                (GLUSTERD_COMP_BASE + 193)

/*!
 * @messageid
 * @diagnosis
 * @recommendedaction
 *
 */
#define GD_MSG_MISSED_SNAP_LIST_EMPTY           (GLUSTERD_COMP_BASE + 194)

/*!
 * @messageid
 * @diagnosis
 * @recommendedaction
 *
 */
#define GD_MSG_SNAP_VOL_RETRIEVE_FAIL           (GLUSTERD_COMP_BASE + 195)

/*!
 * @messageid
 * @diagnosis
 * @recommendedaction
 *
 */
#define GD_MSG_SNAPSHOT_UPDATE_FAIL             (GLUSTERD_COMP_BASE + 196)

/*!
 * @messageid
 * @diagnosis
 * @recommendedaction
 *
 */
#define GD_MSG_SNAPD_PORT_STORE_FAIL            (GLUSTERD_COMP_BASE + 197)

/*!
 * @messageid
 * @diagnosis
 * @recommendedaction
 *
 */
#define GD_MSG_CKSUM_STORE_FAIL                 (GLUSTERD_COMP_BASE + 198)

/*!
 * @messageid
 * @diagnosis
 * @recommendedaction
 *
 */
#define GD_MSG_STORE_HANDLE_CREATE_FAIL         (GLUSTERD_COMP_BASE + 199)

/*!
 * @messageid
 * @diagnosis
 * @recommendedaction
 *
 */
#define GD_MSG_HANDLE_NULL                      (GLUSTERD_COMP_BASE + 200)

/*!
 * @messageid
 * @diagnosis
 * @recommendedaction
 *
 */
#define GD_MSG_VOL_RESTORE_FAIL                 (GLUSTERD_COMP_BASE + 201)

/*!
 * @messageid
 * @diagnosis
 * @recommendedaction
 *
 */
#define GD_MSG_NAME_TOO_LONG                    (GLUSTERD_COMP_BASE + 202)

/*!
 * @messageid
 * @diagnosis
 * @recommendedaction
 *
 */
#define GD_MSG_UUID_PARSE_FAIL                  (GLUSTERD_COMP_BASE + 203)

/*!
 * @messageid
 * @diagnosis
 * @recommendedaction
 *
 */
#define GD_MSG_UNKNOWN_KEY                      (GLUSTERD_COMP_BASE + 204)

/*!
 * @messageid
 * @diagnosis
 * @recommendedaction
 *
 */
#define GD_MSG_STORE_ITER_DESTROY_FAIL          (GLUSTERD_COMP_BASE + 205)

/*!
 * @messageid
 * @diagnosis
 * @recommendedaction
 *
 */
#define GD_MSG_STORE_ITER_GET_FAIL              (GLUSTERD_COMP_BASE + 206)

/*!
 * @messageid
 * @diagnosis
 * @recommendedaction
 *
 */
#define GD_MSG_VOLINFO_UPDATE_FAIL              (GLUSTERD_COMP_BASE + 207)

/*!
 * @messageid
 * @diagnosis
 * @recommendedaction
 *
 */
#define GD_MSG_PARSE_BRICKINFO_FAIL             (GLUSTERD_COMP_BASE + 208)

/*!
 * @messageid
 * @diagnosis
 * @recommendedaction
 *
 */
#define GD_MSG_VERS_STORE_FAIL                  (GLUSTERD_COMP_BASE + 209)

/*!
 * @messageid
 * @diagnosis
 * @recommendedaction
 *
 */
#define GD_MSG_HEADER_ADD_FAIL                  (GLUSTERD_COMP_BASE + 210)

/*!
 * @messageid
 * @diagnosis
 * @recommendedaction
 *
 */
#define GD_MSG_QUOTA_CONF_WRITE_FAIL            (GLUSTERD_COMP_BASE + 211)

/*!
 * @messageid
 * @diagnosis
 * @recommendedaction
 *
 */
#define GD_MSG_QUOTA_CONF_CORRUPT               (GLUSTERD_COMP_BASE + 212)

/*!
 * @messageid
 * @diagnosis
 * @recommendedaction
 *
 */
#define GD_MSG_FORK_FAIL                        (GLUSTERD_COMP_BASE + 213)

/*!
 * @messageid
 * @diagnosis
 * @recommendedaction
 *
 */
#define GD_MSG_CKSUM_COMPUTE_FAIL               (GLUSTERD_COMP_BASE + 214)

/*!
 * @messageid
 * @diagnosis
 * @recommendedaction
 *
 */
#define GD_MSG_VERS_CKSUM_STORE_FAIL            (GLUSTERD_COMP_BASE + 215)

/*!
 * @messageid
 * @diagnosis
 * @recommendedaction
 *
 */
#define GD_MSG_GETXATTR_FAIL                    (GLUSTERD_COMP_BASE + 216)

/*!
 * @messageid
 * @diagnosis
 * @recommendedaction
 *
 */
#define GD_MSG_CONVERSION_FAILED                (GLUSTERD_COMP_BASE + 217)

/*!
 * @messageid
 * @diagnosis
 * @recommendedaction
 *
 */
#define GD_MSG_VOL_NOT_DISTRIBUTE               (GLUSTERD_COMP_BASE + 218)

/*!
 * @messageid
 * @diagnosis
 * @recommendedaction
 *
 */
#define GD_MSG_VOL_STOPPED                      (GLUSTERD_COMP_BASE + 219)

/*!
 * @messageid
 * @diagnosis
 * @recommendedaction
 *
 */
#define GD_MSG_OPCTX_GET_FAIL                   (GLUSTERD_COMP_BASE + 220)

/*!
 * @messageid
 * @diagnosis
 * @recommendedaction
 *
 */
#define GD_MSG_TASKID_GEN_FAIL                  (GLUSTERD_COMP_BASE + 221)

/*!
 * @messageid
 * @diagnosis
 * @recommendedaction
 *
 */
#define GD_MSG_REBALANCE_ID_MISSING             (GLUSTERD_COMP_BASE + 222)

/*!
 * @messageid
 * @diagnosis
 * @recommendedaction
 *
 */
#define GD_MSG_NO_REBALANCE_PFX_IN_VOLNAME      (GLUSTERD_COMP_BASE + 223)

/*!
 * @messageid
 * @diagnosis
 * @recommendedaction
 *
 */
#define GD_MSG_DEFRAG_STATUS_UPDATE_FAIL        (GLUSTERD_COMP_BASE + 224)

/*!
 * @messageid
 * @diagnosis
 * @recommendedaction
 *
 */
#define GD_MSG_UUID_GEN_STORE_FAIL              (GLUSTERD_COMP_BASE + 225)

/*!
 * @messageid
 * @diagnosis
 * @recommendedaction
 *
 */
#define GD_MSG_UUID_STORE_FAIL                  (GLUSTERD_COMP_BASE + 226)

/*!
 * @messageid
 * @diagnosis
 * @recommendedaction
 *
 */
#define GD_MSG_NO_INIT                          (GLUSTERD_COMP_BASE + 227)

/*!
 * @messageid
 * @diagnosis
 * @recommendedaction
 *
 */
#define GD_MSG_MODULE_NOT_INSTALLED             (GLUSTERD_COMP_BASE + 228)

/*!
 * @messageid
 * @diagnosis
 * @recommendedaction
 *
 */
#define GD_MSG_MODULE_NOT_WORKING               (GLUSTERD_COMP_BASE + 229)

/*!
 * @messageid
 * @diagnosis
 * @recommendedaction
 *
 */
#define GD_MSG_WRITE_ACCESS_GRANT_FAIL          (GLUSTERD_COMP_BASE + 230)

/*!
 * @messageid
 * @diagnosis
 * @recommendedaction
 *
 */
#define GD_MSG_DIRPATH_TOO_LONG                 (GLUSTERD_COMP_BASE + 231)

/*!
 * @messageid
 * @diagnosis
 * @recommendedaction
 *
 */
#define GD_MSG_LOGGROUP_INVALID                 (GLUSTERD_COMP_BASE + 232)

/*!
 * @messageid
 * @diagnosis
 * @recommendedaction
 *
 */
#define GD_MSG_DIR_PERM_LIBERAL                 (GLUSTERD_COMP_BASE + 233)

/*!
 * @messageid
 * @diagnosis
 * @recommendedaction
 *
 */
#define GD_MSG_DIR_PERM_STRICT                  (GLUSTERD_COMP_BASE + 234)

/*!
 * @messageid
 * @diagnosis
 * @recommendedaction
 *
 */
#define GD_MSG_MOUNT_SPEC_INSTALL_FAIL          (GLUSTERD_COMP_BASE + 234)

/*!
 * @messageid
 * @diagnosis
 * @recommendedaction
 *
 */
#define GD_MSG_GLUSTERD_SOCK_LISTENER_START_FAIL      (GLUSTERD_COMP_BASE + 235)

/*!
 * @messageid
 * @diagnosis
 * @recommendedaction
 *
 */
#define GD_MSG_DIR_NOT_FOUND                    (GLUSTERD_COMP_BASE + 236)

/*!
 * @messageid
 * @diagnosis
 * @recommendedaction
 *
 */
#define GD_MSG_FAILED_INIT_SHDSVC               (GLUSTERD_COMP_BASE + 237)

/*!
 * @messageid
 * @diagnosis
 * @recommendedaction
 *
 */
#define GD_MSG_FAILED_INIT_NFSSVC               (GLUSTERD_COMP_BASE + 238)

/*!
 * @messageid
 * @diagnosis
 * @recommendedaction
 *
 */
#define GD_MSG_FAILED_INIT_QUOTASVC             (GLUSTERD_COMP_BASE + 239)

/*!
 * @messageid
 * @diagnosis
 * @recommendedaction
 *
 */
#define GD_MSG_RPC_INIT_FAIL                    (GLUSTERD_COMP_BASE + 240)

/*!
 * @messageid
 * @diagnosis
 * @recommendedaction
 *
 */
#define GD_MSG_RPCSVC_REG_NOTIFY_RETURNED       (GLUSTERD_COMP_BASE + 241)

/*!
 * @messageid
 * @diagnosis
 * @recommendedaction
 *
 */
#define GD_MSG_RPC_TRANSPORT_COUNT_GET_FAIL     (GLUSTERD_COMP_BASE + 242)

/*!
 * @messageid
 * @diagnosis
 * @recommendedaction
 *
 */
#define GD_MSG_RPC_LISTENER_CREATE_FAIL         (GLUSTERD_COMP_BASE + 243)

/*!
 * @messageid
 * @diagnosis
 * @recommendedaction
 *
 */
#define GD_MSG_OP_VERS_RESTORE_FAIL               (GLUSTERD_COMP_BASE + 244)

/*!
 * @messageid
 * @diagnosis
 * @recommendedaction
 *
 */
#define GD_MSG_SELF_HEALD_DISABLED              (GLUSTERD_COMP_BASE + 245)

/*!
 * @messageid
 * @diagnosis
 * @recommendedaction
 *
 */
#define GD_MSG_PRIV_NULL                        (GLUSTERD_COMP_BASE + 246)

/*!
 * @messageid
 * @diagnosis
 * @recommendedaction
 *
 */
#define GD_MSG_GSYNC_VALIDATION_FAIL            (GLUSTERD_COMP_BASE + 247)

/*!
 * @messageid
 * @diagnosis
 * @recommendedaction
 *
 */
#define GD_MSG_SLAVE_CONFPATH_DETAILS_FETCH_FAIL     (GLUSTERD_COMP_BASE + 248)

/*!
 * @messageid
 * @diagnosis
 * @recommendedaction
 *
 */
#define GD_MSG_OP_NOT_PERMITTED_AC_REQD         (GLUSTERD_COMP_BASE + 250)

/*!
 * @messageid
 * @diagnosis
 * @recommendedaction
 *
 */
#define GD_MSG_OP_NOT_PERMITTED                 (GLUSTERD_COMP_BASE + 251)

/*!
 * @messageid
 * @diagnosis
 * @recommendedaction
 *
 */
#define GD_MSG_REBALANCE_START_FAIL             (GLUSTERD_COMP_BASE + 252)

/*!
 * @messageid
 * @diagnosis
 * @recommendedaction
 *
 */
#define GD_MSG_NFS_RECONF_FAIL                  (GLUSTERD_COMP_BASE + 253)

/*!
 * @messageid
 * @diagnosis
 * @recommendedaction
 *
 */
#define GD_MSG_REMOVE_BRICK_ID_SET_FAIL         (GLUSTERD_COMP_BASE + 254)

/*!
 * @messageid
 * @diagnosis
 * @recommendedaction
 *
 */
#define GD_MSG_BRICK_MOUNTDIR_GET_FAIL          (GLUSTERD_COMP_BASE + 255)

/*!
 * @messageid
 * @diagnosis
 * @recommendedaction
 *
 */
#define GD_MSG_BRICK_NOT_FOUND                  (GLUSTERD_COMP_BASE + 256)

/*!
 * @messageid
 * @diagnosis
 * @recommendedaction
 *
 */
#define GD_MSG_BRKPATH_TOO_LONG                 (GLUSTERD_COMP_BASE + 257)

/*!
 * @messageid
 * @diagnosis
 * @recommendedaction
 *
 */
#define GD_MSG_CLRLOCKS_CLNT_UMOUNT_FAIL        (GLUSTERD_COMP_BASE + 258)

/*!
 * @messageid
 * @diagnosis
 * @recommendedaction
 *
 */
#define GD_MSG_CLRLOCKS_CLNT_MOUNT_FAIL         (GLUSTERD_COMP_BASE + 259)

/*!
 * @messageid
 * @diagnosis
 * @recommendedaction
 *
 */
#define GD_MSG_CLRLOCKS_MOUNTDIR_CREATE_FAIL    (GLUSTERD_COMP_BASE + 260)

/*!
 * @messageid
 * @diagnosis
 * @recommendedaction
 *
 */
#define GD_MSG_BRK_PORT_NUM_GET_FAIL            (GLUSTERD_COMP_BASE + 261)

/*!
 * @messageid
 * @diagnosis
 * @recommendedaction
 *
 */
#define GD_MSG_BRK_STATEDUMP_FAIL               (GLUSTERD_COMP_BASE + 262)

/*!
 * @messageid
 * @diagnosis
 * @recommendedaction
 *
 */
#define GD_MSG_VOL_GRAPH_CHANGE_NOTIFY_FAIL     (GLUSTERD_COMP_BASE + 263)

/*!
 * @messageid
 * @diagnosis
 * @recommendedaction
 *
 */
#define GD_MSG_INVALID_VG                       (GLUSTERD_COMP_BASE + 264)

/*!
 * @messageid
 * @diagnosis
 * @recommendedaction
 *
 */
#define GD_MSG_GLUSTERD_OP_FAILED               (GLUSTERD_COMP_BASE + 265)

/*!
 * @messageid
 * @diagnosis
 * @recommendedaction
 *
 */
#define GD_MSG_HOSTNAME_ADD_TO_PEERLIST_FAIL    (GLUSTERD_COMP_BASE + 266)

/*!
 * @messageid
 * @diagnosis
 * @recommendedaction
 *
 */
#define GD_MSG_STALE_PEERINFO_REMOVE_FAIL       (GLUSTERD_COMP_BASE + 267)

/*!
 * @messageid
 * @diagnosis
 * @recommendedaction
 *
 */
#define GD_MSG_TRANS_ID_GET_FAIL                (GLUSTERD_COMP_BASE + 268)

/*!
 * @messageid
 * @diagnosis
 * @recommendedaction
 *
 */
#define GD_MSG_RES_DECODE_FAIL                  (GLUSTERD_COMP_BASE + 269)

/*!
 * @messageid
 * @diagnosis
 * @recommendedaction
 *
 */
#define GD_MSG_VOL_ALREADY_EXIST                (GLUSTERD_COMP_BASE + 270)

/*!
 * @messageid
 * @diagnosis
 * @recommendedaction
 *
 */
#define GD_MSG_BAD_BRKORDER                     (GLUSTERD_COMP_BASE + 271)

/*!
 * @messageid
 * @diagnosis
 * @recommendedaction
 *
 */
#define GD_MSG_BAD_BRKORDER_CHECK_FAIL          (GLUSTERD_COMP_BASE + 272)

/*!
 * @messageid
 * @diagnosis
 * @recommendedaction
 *
 */
#define GD_MSG_BRICK_SELECT_FAIL                (GLUSTERD_COMP_BASE + 273)

/*!
 * @messageid
 * @diagnosis
 * @recommendedaction
 *
 */
#define GD_MSG_NO_LOCK_RESP_FROM_PEER           (GLUSTERD_COMP_BASE + 274)

/*!
 * @messageid
 * @diagnosis
 * @recommendedaction
 *
 */
#define GD_MSG_MGMTV3_LOCK_FROM_UUID_REJCT      (GLUSTERD_COMP_BASE + 275)

/*!
 * @messageid
 * @diagnosis
 * @recommendedaction
 *
 */
#define GD_MSG_STAGE_FROM_UUID_REJCT            (GLUSTERD_COMP_BASE + 276)

/*!
 * @messageid
 * @diagnosis
 * @recommendedaction
 *
 */
#define GD_MSG_UNLOCK_FROM_UUID_REJCT           (GLUSTERD_COMP_BASE + 277)

/*!
 * @messageid
 * @diagnosis
 * @recommendedaction
 *
 */
#define GD_MSG_MGMTV3_UNLOCK_FROM_UUID_REJCT    (GLUSTERD_COMP_BASE + 278)

/*!
 * @messageid
 * @diagnosis
 * @recommendedaction
 *
 */
#define GD_MSG_COMMIT_FROM_UUID_REJCT           (GLUSTERD_COMP_BASE + 279)

/*!
 * @messageid
 * @diagnosis
 * @recommendedaction
 *
 */
#define GD_MSG_VOL_NOT_STARTED                  (GLUSTERD_COMP_BASE + 280)

/*!
 * @messageid
 * @diagnosis
 * @recommendedaction
 *
 */
#define GD_MSG_VOL_NOT_REPLICA                  (GLUSTERD_COMP_BASE + 281)

/*!
 * @messageid
 * @diagnosis
 * @recommendedaction
 *
 */
#define GD_MSG_OLD_REMOVE_BRICK_EXISTS          (GLUSTERD_COMP_BASE + 283)

/*!
 * @messageid
 * @diagnosis
 * @recommendedaction
 *
 */
#define GD_MSG_USE_THE_FORCE                    (GLUSTERD_COMP_BASE + 284)

/*!
 * @messageid
 * @diagnosis
 * @recommendedaction
 *
 */
#define GD_MSG_OIP                              (GLUSTERD_COMP_BASE + 285)

/*!
 * @messageid
 * @diagnosis
 * @recommendedaction
 *
 */
#define GD_MSG_OIP_RETRY_LATER                  (GLUSTERD_COMP_BASE + 286)

/*!
 * @messageid
 * @diagnosis
 * @recommendedaction
 *
 */
#define GD_MSG_GSYNC_RESTART_FAIL               (GLUSTERD_COMP_BASE + 287)

/*!
 * @messageid
 * @diagnosis
 * @recommendedaction
 *
 */
#define GD_MSG_LOCK_FROM_UUID_REJCT             (GLUSTERD_COMP_BASE + 288)

/*!
 * @messageid
 * @diagnosis
 * @recommendedaction
 *
 */
#define GD_MSG_BRICK_OP_PAYLOAD_BUILD_FAIL      (GLUSTERD_COMP_BASE + 289)

/*!
 * @messageid
 * @diagnosis
 * @recommendedaction
 *
 */
#define GD_MSG_HOSTNAME_RESOLVE_FAIL            (GLUSTERD_COMP_BASE + 290)

/*!
 * @messageid
 * @diagnosis
 * @recommendedaction
 *
 */
#define GD_MSG_COUNT_VALIDATE_FAILED            (GLUSTERD_COMP_BASE + 291)
/*!
 * @messageid
 * @diagnosis
 * @recommendedaction
 *
 */
#define GD_MSG_SPAWNING_CHILD_FAILED            (GLUSTERD_COMP_BASE + 292)

/*!
 * @messageid
 * @diagnosis
 * @recommendedaction
 *
 */
#define GD_MSG_READ_CHILD_DATA_FAILED           (GLUSTERD_COMP_BASE + 293)

/*!
 * @messageid
 * @diagnosis
 * @recommendedaction
 *
 */
#define GD_MSG_DEFAULT_TEMP_CONFIG              (GLUSTERD_COMP_BASE + 294)

/*!
 * @messageid
 * @diagnosis
 * @recommendedaction
 *
 */
#define GD_MSG_PIDFILE_CREATE_FAILED            (GLUSTERD_COMP_BASE + 295)

/*!
 * @messageid
 * @diagnosis
 * @recommendedaction
 *
 */
#define GD_MSG_GSYNCD_SPAWN_FAILED              (GLUSTERD_COMP_BASE + 296)

/*!
 * @messageid
 * @diagnosis
 * @recommendedaction
 *
 */
#define GD_MSG_SUBOP_NOT_FOUND                  (GLUSTERD_COMP_BASE + 297)

/*!
 * @messageid
 * @diagnosis
 * @recommendedaction
 *
 */
#define GD_MSG_RESERVED_OPTION                  (GLUSTERD_COMP_BASE + 298)

/*!
 * @messageid
 * @diagnosis
 * @recommendedaction
 *
 */
#define GD_MSG_GLUSTERD_PRIV_NOT_FOUND          (GLUSTERD_COMP_BASE + 299)

/*!
 * @messageid
 * @diagnosis
 * @recommendedaction
 *
 */
#define GD_MSG_SLAVEINFO_FETCH_ERROR            (GLUSTERD_COMP_BASE + 300)

/*!
 * @messageid
 * @diagnosis
 * @recommendedaction
 *
 */
#define GD_MSG_VALIDATE_FAILED                  (GLUSTERD_COMP_BASE + 301)

/*!
 * @messageid
 * @diagnosis
 * @recommendedaction
 *
 */
#define GD_MSG_INVOKE_ERROR                     (GLUSTERD_COMP_BASE + 302)

/*!
 * @messageid
 * @diagnosis
 * @recommendedaction
 *
 */
#define GD_MSG_SESSION_CREATE_ERROR             (GLUSTERD_COMP_BASE + 303)

/*!
 * @messageid
 * @diagnosis
 * @recommendedaction
 *
 */
#define GD_MSG_STOP_FORCE                        (GLUSTERD_COMP_BASE + 304)

/*!
 * @messageid
 * @diagnosis
 * @recommendedaction
 *
 */
#define GD_MSG_GET_CONFIG_INFO_FAILED           (GLUSTERD_COMP_BASE + 305)

/*!
 * @messageid
 * @diagnosis
 * @recommendedaction
 *
 */
#define GD_MSG_STAT_FILE_READ_FAILED            (GLUSTERD_COMP_BASE + 306)

/*!
 * @messageid
 * @diagnosis
 * @recommendedaction
 *
 */
#define GD_MSG_CONF_PATH_ASSIGN_FAILED          (GLUSTERD_COMP_BASE + 307)

/*!
 * @messageid
 * @diagnosis
 * @recommendedaction
 *
 */
#define GD_MSG_SESSION_INACTIVE                 (GLUSTERD_COMP_BASE + 308)

/*!
 * @messageid
 * @diagnosis
 * @recommendedaction
 *
 */
#define GD_MSG_PIDFILE_NOT_FOUND                (GLUSTERD_COMP_BASE + 309)

/*!
 * @messageid
 * @diagnosis
 * @recommendedaction
 *
 */
#define GD_MSG_PEER_CMD_ERROR                   (GLUSTERD_COMP_BASE + 310)

/*!
 * @messageid
 * @diagnosis
 * @recommendedaction
 *
 */
#define GD_MSG_SRC_FILE_ERROR                   (GLUSTERD_COMP_BASE + 311)

/*!
 * @messageid
 * @diagnosis
 * @recommendedaction
 *
 */
#define GD_MSG_GET_STATEFILE_NAME_FAILED        (GLUSTERD_COMP_BASE + 312)

/*!
 * @messageid
 * @diagnosis
 * @recommendedaction
 *
 */
#define GD_MSG_STATUS_NULL                      (GLUSTERD_COMP_BASE + 313)

/*!
 * @messageid
 * @diagnosis
 * @recommendedaction
 *
 */
#define GD_MSG_STATUSFILE_CREATE_FAILED         (GLUSTERD_COMP_BASE + 314)

/*!
 * @messageid
 * @diagnosis
 * @recommendedaction
 *
 */
#define GD_MSG_SLAVE_URL_INVALID                (GLUSTERD_COMP_BASE + 315)

/*!
 * @messageid
 * @diagnosis
 * @recommendedaction
 *
 */
#define GD_MSG_INVALID_SLAVE                    (GLUSTERD_COMP_BASE + 316)

/*!
 * @messageid
 * @diagnosis
 * @recommendedaction
 *
 */
#define GD_MSG_READ_ERROR                       (GLUSTERD_COMP_BASE + 317)

/*!
 * @messageid
 * @diagnosis
 * @recommendedaction
 *
 */
#define GD_MSG_ARG_FETCH_ERROR                  (GLUSTERD_COMP_BASE + 318)

/*!
 * @messageid
 * @diagnosis
 * @recommendedaction
 *
 */
#define GD_MSG_REG_FILE_MISSING                 (GLUSTERD_COMP_BASE + 319)

/*!
 * @messageid
 * @diagnosis
 * @recommendedaction
 *
 */
#define GD_MSG_STATEFILE_NAME_NOT_FOUND         (GLUSTERD_COMP_BASE + 320)

/*!
 * @messageid
 * @diagnosis
 * @recommendedaction
 *
 */
#define GD_MSG_GEO_REP_START_FAILED             (GLUSTERD_COMP_BASE + 321)

/*!
 * @messageid
 * @diagnosis
 * @recommendedaction
 *
 */
#define GD_MSG_GSYNCD_ERROR                     (GLUSTERD_COMP_BASE + 322)

/*!
 * @messageid
 * @diagnosis
 * @recommendedaction
 *
 */
#define GD_MSG_UPDATE_STATEFILE_FAILED          (GLUSTERD_COMP_BASE + 323)

/*!
 * @messageid
 * @diagnosis
 * @recommendedaction
 *
 */
#define GD_MSG_STATUS_UPDATE_FAILED             (GLUSTERD_COMP_BASE + 324)

/*!
 * @messageid
 * @diagnosis
 * @recommendedaction
 *
 */
#define GD_MSG_GSYNCD_OP_SET_FAILED             (GLUSTERD_COMP_BASE + 325)

/*!
 * @messageid
 * @diagnosis
 * @recommendedaction
 *
 */
#define GD_MSG_BUFFER_EMPTY                     (GLUSTERD_COMP_BASE + 326)

/*!
 * @messageid
 * @diagnosis
 * @recommendedaction
 *
 */
#define GD_MSG_CONFIG_INFO                      (GLUSTERD_COMP_BASE + 327)

/*!
 * @messageid
 * @diagnosis
 * @recommendedaction
 *
 */
#define GD_MSG_FETCH_CONFIG_VAL_FAILED          (GLUSTERD_COMP_BASE + 328)

/*!
 * @messageid
 * @diagnosis
 * @recommendedaction
 *
 */
#define GD_MSG_GSYNCD_PARSE_ERROR               (GLUSTERD_COMP_BASE + 329)

/*!
 * @messageid
 * @diagnosis
 * @recommendedaction
 *
 */
#define GD_MSG_SESSION_ALREADY_EXIST            (GLUSTERD_COMP_BASE + 330)

/*!
 * @messageid
 * @diagnosis
 * @recommendedaction
 *
 */
#define GD_MSG_FORCE_CREATE_SESSION             (GLUSTERD_COMP_BASE + 331)

/*!
 * @messageid
 * @diagnosis
 * @recommendedaction
 *
 */
#define GD_MSG_GET_KEY_FAILED                   (GLUSTERD_COMP_BASE + 332)

/*!
 * @messageid
 * @diagnosis
 * @recommendedaction
 *
 */
#define GD_MSG_SESSION_DEL_FAILED               (GLUSTERD_COMP_BASE + 333)

/*!
 * @messageid
 * @diagnosis
 * @recommendedaction
 *
 */
#define GD_MSG_CMD_EXEC_FAIL                (GLUSTERD_COMP_BASE + 334)

/*!
 * @messageid
 * @diagnosis
 * @recommendedaction
 *
 */
#define GD_MSG_STRDUP_FAILED                    (GLUSTERD_COMP_BASE + 335)

/*!
 * @messageid
 * @diagnosis
 * @recommendedaction
 *
 */
#define GD_MSG_UNABLE_TO_END                    (GLUSTERD_COMP_BASE + 336)

/*!
 * @messageid
 * @diagnosis
 * @recommendedaction
 *
 */
#define GD_MSG_PAUSE_FAILED                     (GLUSTERD_COMP_BASE + 337)

/*!
 * @messageid
 * @diagnosis
 * @recommendedaction
 *
 */
#define GD_MSG_NORMALIZE_URL_FAIL               (GLUSTERD_COMP_BASE + 338)

/*!
 * @messageid
 * @diagnosis
 * @recommendedaction
 *
 */
#define GD_MSG_MODULE_ERROR                     (GLUSTERD_COMP_BASE + 339)

/*!
 * @messageid
 * @diagnosis
 * @recommendedaction
 *
 */
#define GD_MSG_SLAVEINFO_STORE_ERROR            (GLUSTERD_COMP_BASE + 340)

/*!
 * @messageid
 * @diagnosis
 * @recommendedaction
 *
 */
#define GD_MSG_MARKER_START_FAIL                (GLUSTERD_COMP_BASE + 341)

/*!
 * @messageid
 * @diagnosis
 * @recommendedaction
 *
 */
#define GD_MSG_RESUME_FAILED                    (GLUSTERD_COMP_BASE + 342)

/*!
 * @messageid
 * @diagnosis
 * @recommendedaction
 *
 */
#define GD_MSG_GLUSTERFS_START_FAIL             (GLUSTERD_COMP_BASE + 343)

/*!
 * @messageid
 * @diagnosis
 * @recommendedaction
 *
 */
#define GD_MSG_GLUSTERFS_STOP_FAIL              (GLUSTERD_COMP_BASE + 344)

/*!
 * @messageid
 * @diagnosis
 * @recommendedaction
 *
 */
#define GD_MSG_RBOP_STATE_STORE_FAIL            (GLUSTERD_COMP_BASE + 345)

/*!
 * @messageid
 * @diagnosis
 * @recommendedaction
 *
 */
#define GD_MSG_PUMP_XLATOR_DISABLED             (GLUSTERD_COMP_BASE + 346)

/*!
 * @messageid
 * @diagnosis
 * @recommendedaction
 *
 */
#define GD_MSG_ABORT_OP_FAIL                     (GLUSTERD_COMP_BASE + 347)

/*!
 * @messageid
 * @diagnosis
 * @recommendedaction
 *
 */
#define GD_MSG_PAUSE_OP_FAIL                    (GLUSTERD_COMP_BASE + 348)

/*!
 * @messageid
 * @diagnosis
 * @recommendedaction
 *
 */
#define GD_MSG_GLUSTER_SERVICE_START_FAIL             (GLUSTERD_COMP_BASE + 349)

/*!
 * @messageid
 * @diagnosis
 * @recommendedaction
 *
 */
#define GD_MSG_HANDSHAKE_FAILED                   (GLUSTERD_COMP_BASE + 350)

/*!
 * @messageid
 * @diagnosis
 * @recommendedaction
 *
 */
#define GD_MSG_CLI_REQ_EMPTY                      (GLUSTERD_COMP_BASE + 351)

/*!
 * @messageid
 * @diagnosis
 * @recommendedaction
 *
 */
#define GD_MSG_PEER_ADD_FAIL                       (GLUSTERD_COMP_BASE + 352)

/*!
 * @messageid
 * @diagnosis
 * @recommendedaction
 *
 */
#define GD_MSG_SYNC_FROM_LOCALHOST_UNALLOWED       (GLUSTERD_COMP_BASE + 353)

/*!
 * @messageid
 * @diagnosis
 * @recommendedaction
 *
 */
#define GD_MSG_UUIDS_SAME_RETRY                    (GLUSTERD_COMP_BASE + 354)

/*!
 * @messageid
 * @diagnosis
 * @recommendedaction
 *
 */
#define GD_MSG_TSP_ALREADY_FORMED                  (GLUSTERD_COMP_BASE + 355)

/*!
 * @messageid
 * @diagnosis
 * @recommendedaction
 *
 */
#define GD_MSG_VOLS_ALREADY_PRESENT                (GLUSTERD_COMP_BASE + 356)

/*!
 * @messageid
 * @diagnosis
 * @recommendedaction
 *
 */
#define GD_MSG_REQ_CTX_CREATE_FAIL                 (GLUSTERD_COMP_BASE + 357)

/*!
 * @messageid
 * @diagnosis
 * @recommendedaction
 *
 */
#define GD_MSG_PEER_INFO_UPDATE_FAIL               (GLUSTERD_COMP_BASE + 358)

/*!
 * @messageid
 * @diagnosis
 * @recommendedaction
 *
 */
#define GD_MSG_PEERINFO_CREATE_FAIL                (GLUSTERD_COMP_BASE + 359)

/*!
 * @messageid
 * @diagnosis
 * @recommendedaction
 *
 */
#define GD_MSG_REQ_FROM_UNKNOWN_PEER               (GLUSTERD_COMP_BASE + 360)

/*!
 * @messageid
 * @diagnosis
 * @recommendedaction
 *
 */
#define GD_MSG_STATUS_REPLY_STRING_CREATE_FAIL      (GLUSTERD_COMP_BASE + 361)

/*!
 * @messageid
 * @diagnosis
 * @recommendedaction
 *
 */
#define GD_MSG_TOKENIZE_FAIL                        (GLUSTERD_COMP_BASE + 362)

/*!
 * @messageid
 * @diagnosis
 * @recommendedaction
 *
 */
#define GD_MSG_LAZY_UMOUNT_FAIL                      (GLUSTERD_COMP_BASE + 363)

/*!
 * @messageid
 * @diagnosis
 * @recommendedaction
 *
 */
#define GD_MSG_NFS_SERVER_START_FAIL                 (GLUSTERD_COMP_BASE + 364)

/*!
 * @messageid
 * @diagnosis
 * @recommendedaction
 *
 */
#define GD_MSG_GLUSTER_SERVICES_STOP_FAIL            (GLUSTERD_COMP_BASE + 365)

/*!
 * @messageid
 * @diagnosis
 * @recommendedaction
 *
 */
#define GD_MSG_BRK_CLEANUP_FAIL                      (GLUSTERD_COMP_BASE + 366)

/*!
 * @messageid
 * @diagnosis
 * @recommendedaction
 *
 */
#define GD_MSG_RB_ALREADY_STARTED                    (GLUSTERD_COMP_BASE + 367)

/*!
 * @messageid
 * @diagnosis
 * @recommendedaction
 *
 */
#define GD_MSG_RB_BRICKINFO_GET_FAIL                 (GLUSTERD_COMP_BASE + 368)

/*!
 * @messageid
 * @diagnosis
 * @recommendedaction
 *
 */
#define GD_MSG_BAD_FORMAT                            (GLUSTERD_COMP_BASE + 369)

/*!
 * @messageid
 * @diagnosis
 * @recommendedaction
 *
 */
#define GD_MSG_RB_CMD_FAIL                          (GLUSTERD_COMP_BASE + 370)

/*!
 * @messageid
 * @diagnosis
 * @recommendedaction
 *
 */
#define GD_MSG_RB_NOT_STARTED_OR_PAUSED             (GLUSTERD_COMP_BASE + 371)

/*!
 * @messageid
 * @diagnosis
 * @recommendedaction
 *
 */
#define GD_MSG_RB_NOT_STARTED                       (GLUSTERD_COMP_BASE + 372)

/*!
 * @messageid
 * @diagnosis
 * @recommendedaction
 *
 */
#define GD_MSG_RB_PAUSED_ALREADY                    (GLUSTERD_COMP_BASE + 373)

/*!
 * @messageid
 * @diagnosis
 * @recommendedaction
 *
 */
#define GD_MSG_NO_FREE_PORTS                        (GLUSTERD_COMP_BASE + 374)

/*!
 * @messageid
 * @diagnosis
 * @recommendedaction
 *
 */
#define GD_MSG_EVENT_STATE_TRANSITION_FAIL          (GLUSTERD_COMP_BASE + 375)

/*!
 * @messageid
 * @diagnosis
 * @recommendedaction
 *
 */
#define GD_MSG_HANDLER_RETURNED                     (GLUSTERD_COMP_BASE + 376)

/*!
 * @messageid
 * @diagnosis
 * @recommendedaction
 *
 */
#define GD_MSG_SNAP_COMPARE_CONFLICT                (GLUSTERD_COMP_BASE + 377)

/*!
 * @messageid
 * @diagnosis
 * @recommendedaction
 *
 */
#define GD_MSG_PEER_DETACH_CLEANUP_FAIL             (GLUSTERD_COMP_BASE + 378)

/*!
 * @messageid
 * @diagnosis
 * @recommendedaction
 *
 */
#define GD_MSG_STALE_VOL_REMOVE_FAIL               (GLUSTERD_COMP_BASE + 379)

/*!
 * @messageid
 * @diagnosis
 * @recommendedaction
 *
 */
#define GD_MSG_AC_ERROR                            (GLUSTERD_COMP_BASE + 380)

/*!
 * @messageid
 * @diagnosis
 * @recommendedaction
 *
 */
#define GD_MSG_LOCK_FAIL                           (GLUSTERD_COMP_BASE + 381)

/*!
 * @messageid
 * @diagnosis
 * @recommendedaction
 *
 */
#define GD_MSG_MGMTV3_LOCK_REQ_SEND_FAIL           (GLUSTERD_COMP_BASE + 382)

/*!
 * @messageid
 * @diagnosis
 * @recommendedaction
 *
 */
#define GD_MSG_GLUSTERD_UNLOCK_FAIL                (GLUSTERD_COMP_BASE + 383)

/*!
 * @messageid
 * @diagnosis
 * @recommendedaction
 *
 */
#define GD_MSG_RBOP_START_FAIL                     (GLUSTERD_COMP_BASE + 384)

/*!
 * @messageid
 * @diagnosis
 * @recommendedaction
 *
 */
#define GD_MSG_UNKNOWN_RESPONSE                    (GLUSTERD_COMP_BASE + 385)

/*!
 * @messageid
 * @diagnosis
 * @recommendedaction
 *
 */
#define GD_MSG_COMMIT_REQ_SEND_FAIL                (GLUSTERD_COMP_BASE + 386)

/*!
 * @messageid
 * @diagnosis
 * @recommendedaction
 *
 */
#define GD_MSG_OPCTX_UPDATE_FAIL                   (GLUSTERD_COMP_BASE + 387)

/*!
 * @messageid
 * @diagnosis
 * @recommendedaction
 *
 */
#define GD_MSG_OPCTX_NULL                          (GLUSTERD_COMP_BASE + 388)

/*!
 * @messageid
 * @diagnosis
 * @recommendedaction
 *
 */
#define GD_MSG_DICT_COPY_FAIL                       (GLUSTERD_COMP_BASE + 389)

/*!
 * @messageid
 * @diagnosis
 * @recommendedaction
 *
 */
#define GD_MSG_SHD_STATUS_SET_FAIL                  (GLUSTERD_COMP_BASE + 390)

/*!
 * @messageid
 * @diagnosis
 * @recommendedaction
 *
 */
#define GD_MSG_REPLICA_INDEX_GET_FAIL               (GLUSTERD_COMP_BASE + 391)

/*!
 * @messageid
 * @diagnosis
 * @recommendedaction
 *
 */
#define GD_MSG_NFS_SERVER_NOT_RUNNING               (GLUSTERD_COMP_BASE + 392)

/*!
 * @messageid
 * @diagnosis
 * @recommendedaction
 *
 */
#define GD_MSG_STAGE_REQ_SEND_FAIL                  (GLUSTERD_COMP_BASE + 393)

/*!
 * @messageid
 * @diagnosis
 * @recommendedaction
 *
 */
#define GD_MSG_LOCK_REQ_SEND_FAIL                    (GLUSTERD_COMP_BASE + 394)

/*!
 * @messageid
 * @diagnosis
 * @recommendedaction
 *
 */
#define GD_MSG_VOLNAMES_GET_FAIL                     (GLUSTERD_COMP_BASE + 395)

/*!
 * @messageid
 * @diagnosis
 * @recommendedaction
 *
 */
#define GD_MSG_NO_TASK_ID                            (GLUSTERD_COMP_BASE + 396)

/*!
 * @messageid
 * @diagnosis
 * @recommendedaction
 *
 */
#define GD_MSG_ADD_REMOVE_BRICK_FAIL                 (GLUSTERD_COMP_BASE + 397)

/*!
 * @messageid
 * @diagnosis
 * @recommendedaction
 *
 */
#define GD_MSG_SVC_RESTART_FAIL                      (GLUSTERD_COMP_BASE + 398)

/*!
 * @messageid
 * @diagnosis
 * @recommendedaction
 *
 */
#define GD_MSG_VOL_SET_FAIL                          (GLUSTERD_COMP_BASE + 399)

/*!
 * @messageid
 * @diagnosis
 * @recommendedaction
 *
 */
#define GD_MSG_QUOTAD_NOT_RUNNING                    (GLUSTERD_COMP_BASE + 400)

/*!
 * @messageid
 * @diagnosis
 * @recommendedaction
 *
 */
#define GD_MSG_XLATOR_COUNT_GET_FAIL                (GLUSTERD_COMP_BASE + 401)

/*!
 * @messageid
 * @diagnosis
 * @recommendedaction
 *
 */
#define GD_MSG_TRANS_OPINFO_GET_FAIL                 (GLUSTERD_COMP_BASE + 402)

/*!
 * @messageid
 * @diagnosis
 * @recommendedaction
 *
 */
#define GD_MSG_TRANS_ID_INVALID                      (GLUSTERD_COMP_BASE + 403)

/*!
 * @messageid
 * @diagnosis
 * @recommendedaction
 *
 */
#define GD_MSG_NO_OPTIONS_GIVEN                      (GLUSTERD_COMP_BASE + 404)

/*!
 * @messageid
 * @diagnosis
 * @recommendedaction
 *
 */
#define GD_MSG_SNAPD_NOT_RUNNING                     (GLUSTERD_COMP_BASE + 405)

/*!
 * @messageid
 * @diagnosis
 * @recommendedaction
 *
 */
#define GD_MSG_ADD_ADDRESS_TO_PEER_FAIL              (GLUSTERD_COMP_BASE + 406)

/*!
 * @messageid
 * @diagnosis
 * @recommendedaction
 *
 */
#define GD_MSG_PEER_ADDRESS_GET_FAIL                 (GLUSTERD_COMP_BASE + 407)

/*!
 * @messageid
 * @diagnosis
 * @recommendedaction
 *
 */
#define GD_MSG_GETADDRINFO_FAIL                      (GLUSTERD_COMP_BASE + 408)

/*!
 * @messageid
 * @diagnosis
 * @recommendedaction
 *
 */
#define GD_MSG_PEERINFO_DELETE_FAIL                 (GLUSTERD_COMP_BASE + 409)

/*!
 * @messageid
 * @diagnosis
 * @recommendedaction
 *
 */
#define GD_MSG_KEY_NULL                            (GLUSTERD_COMP_BASE + 410)

/*!
 * @messageid
 * @diagnosis
 * @recommendedaction
 *
 */
#define GD_MSG_SPAWN_SVCS_FAIL                     (GLUSTERD_COMP_BASE + 411)

/*!
 * @messageid
 * @diagnosis
 * @recommendedaction
 *
 */
#define GD_MSG_DICT_ITER_FAIL                     (GLUSTERD_COMP_BASE + 412)

/*!
 * @messageid
 * @diagnosis
 * @recommendedaction
 *
 */
#define GD_MSG_TASK_STATUS_UPDATE_FAIL             (GLUSTERD_COMP_BASE + 413)

/*!
 * @messageid
 * @diagnosis
 * @recommendedaction
 *
 */
#define GD_MSG_VOL_ID_MISMATCH                     (GLUSTERD_COMP_BASE + 414)

/*!
 * @messageid
 * @diagnosis
 * @recommendedaction
 *
 */
#define GD_MSG_STR_TO_BOOL_FAIL                     (GLUSTERD_COMP_BASE + 415)

/*!
 * @messageid
 * @diagnosis
 * @recommendedaction
 *
 */
#define GD_MSG_RB_MNT_BRICKS_MISMATCH                (GLUSTERD_COMP_BASE + 416)

/*!
 * @messageid
 * @diagnosis
 * @recommendedaction
 *
 */
#define GD_MSG_RB_SRC_BRICKS_MISMATCH                 (GLUSTERD_COMP_BASE + 417)

/*!
 * @messageid
 * @diagnosis
 * @recommendedaction
 *
 */
#define GD_MSG_MNTENTRY_GET_FAIL                     (GLUSTERD_COMP_BASE + 418)

/*!
 * @messageid
 * @diagnosis
 * @recommendedaction
 *
 */
#define GD_MSG_INODE_SIZE_GET_FAIL                   (GLUSTERD_COMP_BASE + 419)

/*!
 * @messageid
 * @diagnosis
 * @recommendedaction
 *
 */
#define GD_MSG_NO_STATEFILE_ENTRY                     (GLUSTERD_COMP_BASE + 420)

/*!
 * @messageid
 * @diagnosis
 * @recommendedaction
 *
 */
#define GD_MSG_PMAP_UNSET_FAIL                        (GLUSTERD_COMP_BASE + 421)

/*!
 * @messageid
 * @diagnosis
 * @recommendedaction
 *
 */
#define GD_MSG_GLOBAL_OPT_IMPORT_FAIL                 (GLUSTERD_COMP_BASE + 422)

/*!
 * @messageid
 * @diagnosis
 * @recommendedaction
 *
 */
#define GD_MSD_BRICK_DISCONNECT_FAIL                  (GLUSTERD_COMP_BASE + 423)

/*!
 * @messageid
 * @diagnosis
 * @recommendedaction
 *
 */
#define GD_MSG_SNAP_DETAILS_IMPORT_FAIL               (GLUSTERD_COMP_BASE + 424)

/*!
 * @messageid
 * @diagnosis
 * @recommendedaction
 *
 */
#define GD_MSG_BRICKINFO_CREATE_FAIL                  (GLUSTERD_COMP_BASE + 425)

/*!
 * @messageid
 * @diagnosis
 * @recommendedaction
 *
 */
#define GD_MSG_QUOTA_CKSUM_VER_STORE_FAIL             (GLUSTERD_COMP_BASE + 426)

/*!
 * @messageid
 * @diagnosis
 * @recommendedaction
 *
 */
#define GD_MSG_CKSUM_GET_FAIL                         (GLUSTERD_COMP_BASE + 427)

/*!
 * @messageid
 * @diagnosis
 * @recommendedaction
 *
 */
#define GD_MSG_BRICKPATH_ROOT_GET_FAIL                (GLUSTERD_COMP_BASE + 428)

/*!
 * @messageid
 * @diagnosis
 * @recommendedaction
 *
 */
#define GD_MSG_HOSTNAME_TO_UUID_FAIL                  (GLUSTERD_COMP_BASE + 429)

/*!
 * @messageid
 * @diagnosis
 * @recommendedaction
 *
 */
#define GD_MSG_REPLY_SUBMIT_FAIL                      (GLUSTERD_COMP_BASE + 430)

/*!
 * @messageid
 * @diagnosis
 * @recommendedaction
 *
 */
#define GD_MSG_SERIALIZE_MSG_FAIL                    (GLUSTERD_COMP_BASE + 431)

/*!
 * @messageid
 * @diagnosis
 * @recommendedaction
 *
 */
#define GD_MSG_ENCODE_FAIL                           (GLUSTERD_COMP_BASE + 432)

/*!
 * @messageid
 * @diagnosis
 * @recommendedaction
 *
 */
#define GD_MSG_RB_DST_BRICKS_MISMATCH                (GLUSTERD_COMP_BASE + 433)

/*!
 * @messageid
 * @diagnosis
 * @recommendedaction
 *
 */
#define GD_MSG_XLATOR_VOLOPT_DYNLOAD_ERROR           (GLUSTERD_COMP_BASE + 434)

/*!
 * @messageid
 * @diagnosis
 * @recommendedaction
 *
 */
#define GD_MSG_VOLNAME_NOTFOUND_IN_DICT              (GLUSTERD_COMP_BASE + 435)

/*!
 * @messageid
 * @diagnosis
 * @recommendedaction
 *
 */
#define GD_MSG_FLAGS_NOTFOUND_IN_DICT                (GLUSTERD_COMP_BASE + 436)

/*!
 * @messageid
 * @diagnosis
 * @recommendedactio
 *
 */
#define GD_MSG_HOSTNAME_NOTFOUND_IN_DICT             (GLUSTERD_COMP_BASE + 437)

/*!
 * @messageid
 * @diagnosis
 * @recommendedaction
 *
 */
#define GD_MSG_PORT_NOTFOUND_IN_DICT                  (GLUSTERD_COMP_BASE + 438)

/*!
 * @messageid
 * @diagnosis
 * @recommendedaction
 *
 */
#define GD_MSG_CMDSTR_NOTFOUND_IN_DICT                (GLUSTERD_COMP_BASE + 439)

/*!
 * @messageid
 * @diagnosis
 * @recommendedaction
 *
 */
#define GD_MSG_SNAP_OBJ_NEW_FAIL                    (GLUSTERD_COMP_BASE + 440)

/*!
 * @messageid
 * @diagnosis
 * @recommendedaction
 *
 */
#define GD_MSG_SNAP_BACKEND_MAKE_FAIL               (GLUSTERD_COMP_BASE + 441)

/*!
 * @messageid
 * @diagnosis
 * @recommendedaction
 *
 */
#define GD_MSG_SNAP_CLONE_FAILED                     (GLUSTERD_COMP_BASE + 442)

/*!
 * @messageid
 * @diagnosis
 * @recommendedaction
 *
 */
#define GD_MSG_SNAP_CLONE_PREVAL_FAILED              (GLUSTERD_COMP_BASE + 443)

/*!
 * @messageid
 * @diagnosis
 * @recommendedaction
 *
 */
#define GD_MSG_SNAP_CLONE_POSTVAL_FAILED             (GLUSTERD_COMP_BASE + 444)

/*!
 * @messageid
 * @diagnosis
 * @recommendedaction
 *
 */
#define GD_MSG_VOLINFO_STORE_FAIL                   (GLUSTERD_COMP_BASE + 445)

/*!
 * @messageid
 * @diagnosis
 * @recommendedaction
 *
 */
#define GD_MSG_NEW_FRIEND_SM_EVENT_GET_FAIL         (GLUSTERD_COMP_BASE + 446)

/*!
 * @messageid
 * @diagnosis
 * @recommendedaction
 *
 */
#define GD_MSG_VOL_TYPE_CHANGING_INFO               (GLUSTERD_COMP_BASE + 447)

/*!
 * @messageid
 * @diagnosis
 * @recommendedaction
 *
 */
#define GD_MSG_BRKPATH_MNTPNT_MISMATCH               (GLUSTERD_COMP_BASE + 448)

/*!
 * @messageid
 * @diagnosis
 * @recommendedaction
 *
 */
#define GD_MSG_TASKS_COUNT_MISMATCH                  (GLUSTERD_COMP_BASE + 449)

/*!
 * @messageid
 * @diagnosis
 * @recommendedaction
 *
 */
#define GD_MSG_WRONG_OPTS_SETTING                    (GLUSTERD_COMP_BASE + 450)

/*!
 * @messageid
 * @diagnosis
 * @recommendedaction
 *
 */
#define GD_MSG_PATH_ALREADY_PART_OF_VOL               (GLUSTERD_COMP_BASE + 451)

/*!
 * @messageid
 * @diagnosis
 * @recommendedaction
 *
 */
#define GD_MSG_BRICK_VALIDATE_FAIL                    (GLUSTERD_COMP_BASE + 452)

/*!
 * @messageid
 * @diagnosis
 * @recommendedaction
 *
 */
#define GD_MSG_READIN_FILE_FAILED                     (GLUSTERD_COMP_BASE + 453)

/*!
 * @messageid
 * @diagnosis
 * @recommendedaction
 *
 */
#define GD_MSG_IMPORT_PRDICT_DICT                     (GLUSTERD_COMP_BASE + 454)

/*!
 * @messageid
 * @diagnosis
 * @recommendedaction
 *
 */
#define GD_MSG_VOL_OPTS_IMPORT_FAIL                   (GLUSTERD_COMP_BASE + 455)

/*!
 * @messageid
 * @diagnosis
 * @recommendedaction
 *
 */
#define GD_MSG_BRICK_IMPORT_FAIL                      (GLUSTERD_COMP_BASE + 456)

/*!
 * @messageid
 * @diagnosis
 * @recommendedaction
 *
 */
#define GD_MSG_VOLINFO_IMPORT_FAIL                    (GLUSTERD_COMP_BASE + 457)

/*!
 * @messageid
 * @diagnosis
 * @recommendedaction
 *
 */
#define GD_MSG_BRICK_ID_GEN_FAILED                    (GLUSTERD_COMP_BASE + 458)

/*!
 * @messageid
 * @diagnosis
 * @recommendedaction
 *
 */
#define GD_MSG_GET_STATUS_DATA_FAIL                   (GLUSTERD_COMP_BASE + 459)

/*!
 * @messageid
 * @diagnosis
 * @recommendedaction
 *
 */
#define GD_MSG_BITROT_NOT_RUNNING                     (GLUSTERD_COMP_BASE + 460)

/*!
 * @messageid
 * @diagnosis
 * @recommendedaction
 *
 */
#define GD_MSG_SCRUBBER_NOT_RUNNING                   (GLUSTERD_COMP_BASE + 461)

/*!
 * @messageid
 * @diagnosis
 * @recommendedaction
 *
 */
#define GD_MSG_SRC_BRICK_PORT_UNAVAIL                 (GLUSTERD_COMP_BASE + 462)

/*!
 * @messageid
 * @diagnosis
 * @recommendedaction
 *
 */
#define GD_MSG_BITD_INIT_FAIL                         (GLUSTERD_COMP_BASE + 463)

/*!
 * @messageid
 * @diagnosis
 * @recommendedaction
 *
 */
#define GD_MSG_SCRUB_INIT_FAIL                        (GLUSTERD_COMP_BASE + 464)

/*!
 * @messageid
 * @diagnosis
 * @recommendedaction
 *
 */
#define GD_MSG_VAR_RUN_DIR_INIT_FAIL                  (GLUSTERD_COMP_BASE + 465)

/*!
 * @messageid
 * @diagnosis
 * @recommendedaction
 *
 */
#define GD_MSG_VAR_RUN_DIR_FIND_FAIL                  (GLUSTERD_COMP_BASE + 466)

/*!
 * @messageid
 * @diagnosis
 * @recommendedaction
 *
 */
#define GD_MSG_SCRUBSVC_RECONF_FAIL                  (GLUSTERD_COMP_BASE + 467)

/*!
 * @messageid
 * @diagnosis
 * @recommendedaction
 *
 */
#define GD_MSG_BITDSVC_RECONF_FAIL                   (GLUSTERD_COMP_BASE + 468)

/*!
 * @messageid
 * @diagnosis
 * @recommendedaction
 *
 */
#define GD_MSG_NFS_GNS_START_FAIL                    (GLUSTERD_COMP_BASE + 469)

/*!
 * @messageid
 * @diagnosis
 * @recommendedaction
 *
 */
#define GD_MSG_NFS_GNS_SETUP_FAIL                    (GLUSTERD_COMP_BASE + 470)

/*!
 * @messageid
 * @diagnosis
 * @recommendedaction
 *
 */
#define GD_MSG_UNRECOGNIZED_SVC_MNGR                 (GLUSTERD_COMP_BASE + 471)

/*!
 * @messageid
 * @diagnosis
 * @recommendedaction
 *
 */
#define GD_MSG_NFS_GNS_OP_HANDLE_FAIL                 (GLUSTERD_COMP_BASE + 472)

/*!
 * @messageid
 * @diagnosis
 * @recommendedaction
 *
 */
#define GD_MSG_EXPORT_FILE_CREATE_FAIL                (GLUSTERD_COMP_BASE + 473)

/*!
 * @messageid
 * @diagnosis
 * @recommendedaction
 *
 */
#define GD_MSG_NFS_GNS_HOST_FOUND                     (GLUSTERD_COMP_BASE + 474)

/*!
 * @messageid
 * @diagnosis
 * @recommendedaction
 *
 */
#define GD_MSG_REBALANCE_CMD_IN_TIER_VOL              (GLUSTERD_COMP_BASE + 475)

/*!
 * @messageid
 * @diagnosis
 * @recommendedaction
 *
 */
#define GD_MSG_INCOMPATIBLE_VALUE                     (GLUSTERD_COMP_BASE + 476)

/*!
 * @messageid
 * @diagnosis
 * @recommendedaction
 *
 */
#define GD_MSG_GENERATED_UUID                        (GLUSTERD_COMP_BASE + 477)

/*!
 * @messageid
 * @diagnosis
 * @recommendedaction
 *
 */
#define GD_MSG_FILE_DESC_LIMIT_SET                   (GLUSTERD_COMP_BASE + 478)

/*!
 * @messageid
 * @diagnosis
 * @recommendedaction
 *
 */
#define GD_MSG_CURR_WORK_DIR_INFO                   (GLUSTERD_COMP_BASE + 479)

/*!
 * @messageid
 * @diagnosis
 * @recommendedaction
 *
 */
#define GD_MSG_STRIPE_COUNT_CHANGE_INFO              (GLUSTERD_COMP_BASE + 480)

/*!
 * @messageid
 * @diagnosis
 * @recommendedaction
 *
 */
#define GD_MSG_REPLICA_COUNT_CHANGE_INFO             (GLUSTERD_COMP_BASE + 481)

/*!
 * @messageid
 * @diagnosis
 * @recommendedaction
 *
 */
#define GD_MSG_ADD_BRICK_REQ_RECVD                   (GLUSTERD_COMP_BASE + 482)

/*!
 * @messageid
 * @diagnosis
 * @recommendedaction
 *
 */
#define GD_MSG_VOL_ALREADY_TIER                      (GLUSTERD_COMP_BASE + 483)

/*!
 * @messageid
 * @diagnosis
 * @recommendedaction
 *
 */
#define GD_MSG_REM_BRICK_REQ_RECVD                   (GLUSTERD_COMP_BASE + 484)

/*!
 * @messageid
 * @diagnosis
 * @recommendedaction
 *
 */
#define GD_MSG_VOL_NOT_TIER                         (GLUSTERD_COMP_BASE + 485)

/*!
 * @messageid
 * @diagnosis
 * @recommendedaction
 *
 */
#define GD_MSG_LOG_ROTATE_REQ_RECVD                 (GLUSTERD_COMP_BASE + 486)

/*!
 * @messageid
 * @diagnosis
 * @recommendedaction
 *
 */
#define GD_MSG_CLI_REQ_RECVD                        (GLUSTERD_COMP_BASE + 487)

/*!
 * @messageid
 * @diagnosis
 * @recommendedaction
 *
 */
#define GD_MSG_GET_VOL_REQ_RCVD                     (GLUSTERD_COMP_BASE + 488)

/*!
 * @messageid
 * @diagnosis
 * @recommendedaction
 *
 */
#define GD_MSG_VOL_SYNC_REQ_RCVD                     (GLUSTERD_COMP_BASE + 489)

/*!
 * @messageid
 * @diagnosis
 * @recommendedaction
 *
 */
#define GD_MSG_PROBE_RCVD                            (GLUSTERD_COMP_BASE + 490)

/*!
 * @messageid
 * @diagnosis
 * @recommendedaction
 *
 */
#define GD_MSG_UNFRIEND_REQ_RCVD                     (GLUSTERD_COMP_BASE + 491)

/*!
 * @messageid
 * @diagnosis
 * @recommendedaction
 *
 */
#define GD_MSG_FRIEND_UPDATE_RCVD                   (GLUSTERD_COMP_BASE + 492)

/*!
 * @messageid
 * @diagnosis
 * @recommendedaction
 *
 */
#define GD_MSG_RESPONSE_INFO                         (GLUSTERD_COMP_BASE + 493)

/*!
 * @messageid
 * @diagnosis
 * @recommendedaction
 *
 */
#define GD_MSG_VOL_PROFILE_REQ_RCVD                  (GLUSTERD_COMP_BASE + 494)

/*!
 * @messageid
 * @diagnosis
 * @recommendedaction
 *
 */
#define GD_MSG_GETWD_REQ_RCVD                       (GLUSTERD_COMP_BASE + 495)

/*!
 * @messageid
 * @diagnosis
 * @recommendedaction
 *
 */
#define GD_MSG_MOUNT_REQ_RCVD                       (GLUSTERD_COMP_BASE + 496)

/*!
 * @messageid
 * @diagnosis
 * @recommendedaction
 *
 */
#define GD_MSG_UMOUNT_REQ_RCVD                       (GLUSTERD_COMP_BASE + 497)

/*!
 * @messageid
 * @diagnosis
 * @recommendedaction
 *
 */
#define GD_MSG_CONNECT_RETURNED                       (GLUSTERD_COMP_BASE + 498)

/*!
 * @messageid
 * @diagnosis
 * @recommendedaction
 *
 */
#define GD_MSG_STATUS_VOL_REQ_RCVD                    (GLUSTERD_COMP_BASE + 499)

/*!
 * @messageid
 * @diagnosis
 * @recommendedaction
 *
 */
#define GD_MSG_CLRCLK_VOL_REQ_RCVD                    (GLUSTERD_COMP_BASE + 500)

/*!
 * @messageid
 * @diagnosis
 * @recommendedaction
 *
 */
#define GD_MSG_BARRIER_VOL_REQ_RCVD                   (GLUSTERD_COMP_BASE + 501)

/*!
 * @messageid
 * @diagnosis
 * @recommendedaction
 *
 */
#define GD_MSG_UUID_RECEIVED                          (GLUSTERD_COMP_BASE + 502)

/*!
 * @messageid
 * @diagnosis
 * @recommendedaction
 *
 */
#define GD_MSG_REPLACE_BRK_COMMIT_FORCE_REQ_RCVD      (GLUSTERD_COMP_BASE + 503)

/*!
 * @messageid
 * @diagnosis
 * @recommendedaction
 *
 */
#define GD_MSG_BRK_PORT_NO_ADD_INDO                   (GLUSTERD_COMP_BASE + 504)

/*!
 * @messageid
 * @diagnosis
 * @recommendedaction
 *
 */
#define GD_MSG_REPLACE_BRK_REQ_RCVD                   (GLUSTERD_COMP_BASE + 505)

/*!
 * @messageid
 * @diagnosis
 * @recommendedaction
 *
 */
#define GD_MSG_ADD_OP_ARGS_FAIL                      (GLUSTERD_COMP_BASE + 506)

/*!
 * @messageid
 * @diagnosis
 * @recommendedaction
 *
 */
#define GD_MSG_POST_HOOK_STUB_INIT_FAIL              (GLUSTERD_COMP_BASE + 507)

/*!
 * @messageid
 * @diagnosis
 * @recommendedaction
 *
 */
#define GD_MSG_HOOK_STUB_NULL                       (GLUSTERD_COMP_BASE + 508)

/*!
 * @messageid
 * @diagnosis
 * @recommendedaction
 *
 */
#define GD_MSG_SPAWN_THREADS_FAIL                   (GLUSTERD_COMP_BASE + 509)

/*!
 * @messageid
 * @diagnosis
 * @recommendedaction
 *
 */
#define GD_MSG_STALE_VOL_DELETE_INFO                (GLUSTERD_COMP_BASE + 510)

/*!
 * @messageid
 * @diagnosis
 * @recommendedaction
 *
 */
#define GD_MSG_PROBE_REQ_RESP_RCVD                  (GLUSTERD_COMP_BASE + 511)

/*!
 * @messageid
 * @diagnosis
 * @recommendedaction
 *
 */
#define GD_MSG_HOST_PRESENT_ALREADY                  (GLUSTERD_COMP_BASE + 512)

/*!
 * @messageid
 * @diagnosis
 * @recommendedaction
 *
 */
#define GD_MSG_OP_VERS_INFO                          (GLUSTERD_COMP_BASE + 513)

/*!
 * @messageid
 * @diagnosis
 * @recommendedaction
 *
 */
#define GD_MSG_OP_VERS_SET_INFO                      (GLUSTERD_COMP_BASE + 514)

/*!
 * @messageid
 * @diagnosis
 * @recommendedaction
 *
 */
#define GD_MSG_NEW_NODE_STATE_CREATION               (GLUSTERD_COMP_BASE + 515)

/*!
 * @messageid
 * @diagnosis
 * @recommendedaction
 *
 */
#define GD_MSG_ALREADY_MOUNTED                      (GLUSTERD_COMP_BASE + 516)

/*!
 * @messageid
 * @diagnosis
 * @recommendedaction
 *
 */
#define GD_MSG_SHARED_STRG_VOL_OPT_VALIDATE_FAIL     (GLUSTERD_COMP_BASE + 517)

/*!
 * @messageid
 * @diagnosis
 * @recommendedaction
 *
 */
#define GD_MSG_NFS_GNS_STOP_FAIL                     (GLUSTERD_COMP_BASE + 518)

/*!
 * @messageid
 * @diagnosis
 * @recommendedaction
 *
 */
#define GD_MSG_NFS_GNS_RESET_FAIL                     (GLUSTERD_COMP_BASE + 519)

/*!
 * @messageid
 * @diagnosis
 * @recommendedaction
 *
 */
#define GD_MSG_SHARED_STRG_SET_FAIL                   (GLUSTERD_COMP_BASE + 520)

/*!
 * @messageid
 * @diagnosis
 * @recommendedaction
 *
 */
#define GD_MSG_VOL_TRANSPORT_TYPE_CHANGE             (GLUSTERD_COMP_BASE + 521)

/*!
 * @messageid
 * @diagnosis
 * @recommendedaction
 *
 */
#define GD_MSG_PEER_COUNT_GET_FAIL                   (GLUSTERD_COMP_BASE + 522)

/*!
 * @messageid
 * @diagnosis
 * @recommendedaction
 *
 */
#define GD_MSG_INSUFFICIENT_UP_NODES                (GLUSTERD_COMP_BASE + 523)

/*!
 * @messageid
 * @diagnosis
 * @recommendedaction
 *
 */
#define GD_MSG_OP_STAGE_STATS_VOL_FAIL              (GLUSTERD_COMP_BASE + 524)

/*!
 * @messageid
 * @diagnosis
 * @recommendedaction
 *
 */
#define GD_MSG_VOL_ID_SET_FAIL                     (GLUSTERD_COMP_BASE + 525)

/*!
 * @messageid
 * @diagnosis
 * @recommendedaction
 *
 */
#define GD_MSG_OP_STAGE_RESET_VOL_FAIL                (GLUSTERD_COMP_BASE + 526)

/*!
 * @messageid
 * @diagnosis
 * @recommendedaction
 *
 */
#define GD_MSG_OP_STAGE_BITROT_FAIL                  (GLUSTERD_COMP_BASE + 527)

/*!
 * @messageid
 * @diagnosis
 * @recommendedaction
 *
 */
#define GD_MSG_OP_STAGE_QUOTA_FAIL                    (GLUSTERD_COMP_BASE + 528)

/*!
 * @messageid
 * @diagnosis
 * @recommendedaction
 *
 */
#define GD_MSG_OP_STAGE_DELETE_VOL_FAIL               (GLUSTERD_COMP_BASE + 529)

/*!
 * @messageid
 * @diagnosis
 * @recommendedaction
 *
 */
#define GD_MSG_HANDLE_HEAL_CMD_FAIL                   (GLUSTERD_COMP_BASE + 530)

/*!
 * @messageid
 * @diagnosis
 * @recommendedaction
 *
 */
#define GD_MSG_CLRCLK_SND_CMD_FAIL                   (GLUSTERD_COMP_BASE + 531)

/*!
 * @messageid
 * @diagnosis
 * @recommendedaction
 *
 */
#define GD_MSG_DISPERSE_CLUSTER_FOUND               (GLUSTERD_COMP_BASE + 532)

/*!
 * @messageid
 * @diagnosis
 * @recommendedaction
 *
 */
#define GD_MSG_HEAL_VOL_REQ_RCVD                     (GLUSTERD_COMP_BASE + 533)

/*!
 * @messageid
 * @diagnosis
 * @recommendedaction
 *
 */
#define GD_MSG_STATEDUMP_VOL_REQ_RCVD               (GLUSTERD_COMP_BASE + 534)

/*!
 * @messageid
 * @diagnosis
 * @recommendedaction
 *
 */
#define GD_MSG_THINPOOLS_FOR_THINLVS                 (GLUSTERD_COMP_BASE + 535)

/*!
 * @messageid
 * @diagnosis
 * @recommendedaction
 *
 */
#define GD_MSG_OP_STAGE_CREATE_VOL_FAIL              (GLUSTERD_COMP_BASE + 536)

/*!
 * @messageid
 * @diagnosis
 * @recommendedaction
 *
 */
#define GD_MSG_OP_STAGE_START_VOL_FAIL              (GLUSTERD_COMP_BASE + 537)

/*!
 * @messageid
 * @diagnosis
 * @recommendedaction
 *
 */
#define GD_MSG_NFS_GNS_UNEXPRT_VOL_FAIL              (GLUSTERD_COMP_BASE + 538)

/*!
 * @messageid
 * @diagnosis
 * @recommendedaction
 *
 */
#define GD_MSG_TASK_ID_INFO                          (GLUSTERD_COMP_BASE + 539)

/*!
 * @messageid
 * @diagnosis
 * @recommendedaction
 *
 */
#define GD_MSG_DEREGISTER_SUCCESS                    (GLUSTERD_COMP_BASE + 540)

/*!
 * @messageid
 * @diagnosis
 * @recommendedaction
 *
 */
#define GD_MSG_STATEDUMP_OPTS_RCVD                    (GLUSTERD_COMP_BASE + 541)

/*!
 * @messageid
 * @diagnosis
 * @recommendedaction
 *
 */
#define GD_MSG_STATEDUMP_INFO                         (GLUSTERD_COMP_BASE + 542)

/*!
 * @messageid
 * @diagnosis
 * @recommendedaction
 *
 */
#define GD_MSG_RECOVERING_CORRUPT_CONF                (GLUSTERD_COMP_BASE + 543)

/*!
 * @messageid
 * @diagnosis
 * @recommendedaction
 *
 */
#define GD_MSG_RETRIEVED_UUID                        (GLUSTERD_COMP_BASE + 544)

/*!
 * @messageid
 * @diagnosis
 * @recommendedaction
 *
 */
#define GD_MSG_XLATOR_CREATE_FAIL                     (GLUSTERD_COMP_BASE + 545)

/*!
 * @messageid
 * @diagnosis
 * @recommendedaction
 *
 */
#define GD_MSG_GRAPH_ENTRY_ADD_FAIL                   (GLUSTERD_COMP_BASE + 546)

/*!
 * @messageid
 * @diagnosis
 * @recommendedaction
 *
 */
#define GD_MSG_ERROR_ENCOUNTERED                      (GLUSTERD_COMP_BASE + 547)

/*!
 * @messageid
 * @diagnosis
 * @recommendedaction
 *
 */
#define GD_MSG_FILTER_RUN_FAILED                      (GLUSTERD_COMP_BASE + 548)

/*!
 * @messageid
 * @diagnosis
 * @recommendedaction
 *
 */
#define GD_MSG_DEFAULT_OPT_INFO                       (GLUSTERD_COMP_BASE + 549)

/*!
 * @messageid
 * @diagnosis
 * @recommendedaction
 *
 */
#define GD_MSG_MARKER_STATUS_GET_FAIL                 (GLUSTERD_COMP_BASE + 550)

/*!
 * @messageid
 * @diagnosis
 * @recommendedaction
 *
 */
#define GD_MSG_MARKER_DISABLE_FAIL                    (GLUSTERD_COMP_BASE + 551)

/*!
 * @messageid
 * @diagnosis
 * @recommendedaction
 *
 */
#define GD_MSG_GRAPH_FEATURE_ADD_FAIL                 (GLUSTERD_COMP_BASE + 552)

/*!
 * @messageid
 * @diagnosis
 * @recommendedaction
 *
 */
#define GD_MSG_XLATOR_SET_OPT_FAIL                   (GLUSTERD_COMP_BASE + 553)

/*!
 * @messageid
 * @diagnosis
 * @recommendedaction
 *
 */
#define GD_MSG_BUILD_GRAPH_FAILED                   (GLUSTERD_COMP_BASE + 554)

/*!
 * @messageid
 * @diagnosis
 * @recommendedaction
 *
 */
#define GD_MSG_XML_TEXT_WRITE_FAIL                  (GLUSTERD_COMP_BASE + 555)

/*!
 * @messageid
 * @diagnosis
 * @recommendedaction
 *
 */
#define GD_MSG_XML_DOC_START_FAIL                   (GLUSTERD_COMP_BASE + 556)

/*!
 * @messageid
 * @diagnosis
 * @recommendedaction
 *
 */
#define GD_MSG_XML_ELE_CREATE_FAIL                  (GLUSTERD_COMP_BASE + 557)

/*!
 * @messageid
 * @diagnosis
 * @recommendedaction
 *
 */
#define GD_MSG_VOLUME_INCONSISTENCY                  (GLUSTERD_COMP_BASE + 558)

/*!
 * @messageid
 * @diagnosis
 * @recommendedaction
 *
 */
#define GD_MSG_XLATOR_LINK_FAIL                     (GLUSTERD_COMP_BASE + 559)

/*!
 * @messageid
 * @diagnosis
 * @recommendedaction
 *
 */
#define GD_MSG_REMOTE_HOST_GET_FAIL                 (GLUSTERD_COMP_BASE + 560)

/*!
 * @messageid
 * @diagnosis
 * @recommendedaction
 *
 */
#define GD_MSG_GRAPH_SET_OPT_FAIL                    (GLUSTERD_COMP_BASE + 561)

/*!
 * @messageid
 * @diagnosis
 * @recommendedaction
 *
 */
#define GD_MSG_ROOT_SQUASH_ENABLED                    (GLUSTERD_COMP_BASE + 562)

/*!
 * @messageid
 * @diagnosis
 * @recommendedaction
 *
 */
#define GD_MSG_ROOT_SQUASH_FAILED                     (GLUSTERD_COMP_BASE + 563)

/*!
 * @messageid
 * @diagnosis
 * @recommendedaction
 *
 */
#define GD_MSG_LOCK_OWNER_MISMATCH                    (GLUSTERD_COMP_BASE + 564)

/*!
 * @messageid
 * @diagnosis
 * @recommendedaction
 *
 */
#define GD_MSG_LOCK_NOT_HELD                          (GLUSTERD_COMP_BASE + 565)

/*!
 * @messageid
 * @diagnosis
 * @recommendedaction
 *
 */
#define GD_MSG_LOCK_ALREADY_HELD                      (GLUSTERD_COMP_BASE + 566)

/*!
 * @messageid
 * @diagnosis
 * @recommendedaction
 *
 */
#define GD_MSG_SVC_START_SUCCESS                     (GLUSTERD_COMP_BASE + 567)

/*!
 * @messageid
 * @diagnosis
 * @recommendedaction
 *
 */
#define GD_MSG_SVC_STOP_SUCCESS                     (GLUSTERD_COMP_BASE + 568)

/*!
 * @messageid
 * @diagnosis
 * @recommendedaction
 *
 */
#define GD_MSG_PARAM_NULL                           (GLUSTERD_COMP_BASE + 569)

/*!
 * @messageid
 * @diagnosis
 * @recommendedaction
 *
 */
#define GD_MSG_SVC_STOP_FAIL                        (GLUSTERD_COMP_BASE + 570)

/*!
 * @messageid
 * @diagnosis
 * @recommendedaction
 *
 */

#define GD_MSG_SHARED_STORAGE_DOES_NOT_EXIST        (GLUSTERD_COMP_BASE + 571)

/*!
 * @messageid
 * @diagnosis
 * @recommendedaction
 *
 */

#define GD_MSG_SNAP_PAUSE_TIER_FAIL                 (GLUSTERD_COMP_BASE + 572)

/*!
 * @messageid
 * @diagnosis
 * @recommendedaction
 *
 */
#define GD_MSG_SNAP_RESUME_TIER_FAIL                (GLUSTERD_COMP_BASE + 573)

/*!
 * @messageid
 * @diagnosis
 * @recommendedaction
 *
 */
#define GD_MSG_FILE_NOT_FOUND                       (GLUSTERD_COMP_BASE + 574)

/*!
 * @messageid 106575
 * @diagnosis  Brick failed to start with given port, hence it gets a fresh port
 *             on its own and try to restart the brick with a new port
 * @recommendedaction  Ensure the new port is not blocked by firewall
 */
#define GD_MSG_RETRY_WITH_NEW_PORT                   (GLUSTERD_COMP_BASE + 575)

/*!
 * @messageid
 * @diagnosis
 * @recommendedaction
 *
 */
#define GD_MSG_REMOTE_VOL_UUID_FAIL                (GLUSTERD_COMP_BASE + 576)

/*!
 * @messageid
 * @diagnosis
 * @recommendedaction
 *
 */
#define GD_MSG_SLAVE_VOL_PARSE_FAIL               (GLUSTERD_COMP_BASE + 577)

/*!
 * @messageid
 * @diagnosis
 * @recommendedaction
 *
 */
#define GD_MSG_DICT_GET_SUCCESS                   (GLUSTERD_COMP_BASE + 578)

/*!
 * @messageid
 * @diagnosis
 * @recommendedaction
 *
 */
#define GD_MSG_PMAP_REGISTRY_REMOVE_FAIL          (GLUSTERD_COMP_BASE + 579)

/*!
 * @messageid
 * @diagnosis
 * @recommendedaction
 *
 */
#define GD_MSG_MNTBROKER_LABEL_NULL                (GLUSTERD_COMP_BASE + 580)

/*!
 * @messageid
 * @diagnosis
 * @recommendedaction
 *
 */
#define GD_MSG_MNTBROKER_LABEL_MISS                (GLUSTERD_COMP_BASE + 581)

/*!
 * @messageid
 * @diagnosis
 * @recommendedaction
 *
 */
#define GD_MSG_MNTBROKER_SPEC_MISMATCH            (GLUSTERD_COMP_BASE + 582)

/*!
 * @messageid
 * @diagnosis
 * @recommendedaction
 *
 */
#define GD_MSG_SYSCALL_FAIL                          (GLUSTERD_COMP_BASE + 583)

/*!
 * @messageid
 * @diagnosis
 * @recommendation
 *
 */
#define GD_MSG_DAEMON_STATE_REQ_RCVD              (GLUSTERD_COMP_BASE + 584)

/*!
 * @messageid
 * @diagnosis
 * @recommendation
 *
 */
#define GD_MSG_BRICK_CLEANUP_SUCCESS               (GLUSTERD_COMP_BASE + 585)

/*!
 * @messageid
 * @diagnosis
 * @recommendation
 *
 */
#define GD_MSG_STATE_STR_GET_FAILED               (GLUSTERD_COMP_BASE + 586)

/*!
 * @messageid
 * @diagnosis
 * @recommendedaction
 *
 */
#define GD_MSG_RESET_BRICK_COMMIT_FORCE_REQ_RCVD   (GLUSTERD_COMP_BASE + 587)

/*!
 * @messageid
 * @diagnosis
 * @recommendedaction
 *
 */
#define GD_MSG_RESET_BRICK_CMD_FAIL                (GLUSTERD_COMP_BASE + 588)

/*!
 * @messageid
 * @diagnosis
 * @recommendedaction
 *
 */
#define GD_MSG_NFS_GANESHA_DISABLED                (GLUSTERD_COMP_BASE + 589)

/*!
 * @messageid
 * @diagnosis
 * @recommendedaction
 *
 */
#define GD_MSG_TIERD_STOP_FAIL                      (GLUSTERD_COMP_BASE + 590)

/*!
 * @messageid
 * @diagnosis
 * @recommendedaction
 *
 */
#define GD_MSG_TIERD_CREATE_FAIL                    (GLUSTERD_COMP_BASE + 591)

/*!
 * @messageid
 * @diagnosis
 * @recommendedaction
 *
 */
#define GD_MSG_TIERD_START_FAIL                     (GLUSTERD_COMP_BASE + 592)

/*!
 * @messageid
 * @diagnosis
 * @recommendedaction
 *
 */
#define GD_MSG_TIERD_OBJ_GET_FAIL                   (GLUSTERD_COMP_BASE + 593)

/*!
 * @messageid
 * @diagnosis
 * @recommendedaction
 *
 */
#define GD_MSG_TIERD_NOT_RUNNING                     (GLUSTERD_COMP_BASE + 594)

/*!
 * @messageid
 * @diagnosis
 * @recommendedaction
 *
 */
#define GD_MSG_TIERD_INIT_FAIL                       (GLUSTERD_COMP_BASE + 595)

/*!
 * @messageid
 * @diagnosis
 * @recommendedaction
 *
 */

/*------------*/

#define GD_MSG_BRICK_MX_SET_FAIL                   (GLUSTERD_COMP_BASE + 596)
/*!
 * @messageid
 * @diagnosis
 * @recommendedaction
 *
 */

#define GD_MSG_NO_SIG_TO_PID_ZERO                  (GLUSTERD_COMP_BASE + 597)

/*------------*/

#define glfs_msg_end_x GLFS_MSGID_END, "Invalid: End of messages"
#endif /* !_GLUSTERD_MESSAGES_H_ */
