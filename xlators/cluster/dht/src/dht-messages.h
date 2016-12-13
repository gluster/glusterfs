/*Copyright (c) 2015 Red Hat, Inc. <http://www.redhat.com>
  This file is part of GlusterFS.

  This file is licensed to you under your choice of the GNU Lesser
  General Public License, version 3 or any later version (LGPLv3 or
  later), or the GNU General Public License, version 2 (GPLv2), in all
  cases as published by the Free Software Foundation.
*/

#ifndef _DHT_MESSAGES_H_
#define _DHT_MESSAGES_H_

#include "glfs-message-id.h"

/*! \file dht-messages.h
 *  \brief DHT log-message IDs and their descriptions
 *
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

#define GLFS_DHT_BASE                   GLFS_MSGID_COMP_DHT
#define GLFS_DHT_NUM_MESSAGES           118
#define GLFS_MSGID_END          (GLFS_DHT_BASE + GLFS_DHT_NUM_MESSAGES + 1)

/* Messages with message IDs */
#define glfs_msg_start_x GLFS_DHT_BASE, "Invalid: Start of messages"




/*!
 * @messageid 109001
 * @diagnosis   Cached subvolume could not be found for the specified
 *              path
 * @recommendedaction  None
 *
 */

#define DHT_MSG_CACHED_SUBVOL_GET_FAILED        (GLFS_DHT_BASE + 1)

/*!
 * @messageid 109002
 * @diagnosis Linkfile creation failed
 * @recommendedaction  None
 *
 */

#define DHT_MSG_CREATE_LINK_FAILED      (GLFS_DHT_BASE + 2)

/*!
 * @messageid 109003
 * @diagnosis The value could not be set for the specified key in
 *       the dictionary
 *
 * @recommendedaction  None
 *
 */

#define DHT_MSG_DICT_SET_FAILED         (GLFS_DHT_BASE + 3)

/*!
 * @messageid 109004
 * @diagnosis Directory attributes could not be healed
 * @recommendedaction  None
 *
 */

#define DHT_MSG_DIR_ATTR_HEAL_FAILED    (GLFS_DHT_BASE + 4)

/*!
 * @messageid 109005
 * @diagnosis Self-heal failed for the specified directory
 * @recommendedaction  Ensure that all subvolumes are online
 *              and reachable and perform a lookup operation
 *              on the directory again.
 *
 */

#define DHT_MSG_DIR_SELFHEAL_FAILED     (GLFS_DHT_BASE + 5)

/*!
 * @messageid 109006
 * @diagnosis The extended attributes could not be healed for
 *            the specified directory on the specified subvolume
 *
 * @recommendedaction  None
 *
 */

#define DHT_MSG_DIR_SELFHEAL_XATTR_FAILED       (GLFS_DHT_BASE + 6)

/*!
 * @messageid 109007
 * @diagnosis   A lookup operation found the file with the same path
 *      on multiple subvolumes.
 * @recommendedaction
 *      1. Create backups of the file on other subvolumes.
 *      2. Inspect the content of the files to identify
 *                      and retain the most appropriate file.
 *
 */

#define DHT_MSG_FILE_ON_MULT_SUBVOL     (GLFS_DHT_BASE + 7)

/*!
 * @messageid 109008
 * @diagnosis A path resolves to a file on one subvolume and a directory
 *             on another
 * @recommendedaction
 *              1. Create a backup of the file with a different name
 *              and delete the original file.
 *              2. In the newly created back up file, remove the "trusted.gfid"
 *                      extended attribute.
 *                - Command: setfattr -x "trusted.gfid" \<path to the newly created backup file\>
 *              3. Perform a new lookup operation on both the new and old paths.
 *              4. From the mount point, inspect both the paths and retain the
 *              relevant file or directory.
 *
 */

#define DHT_MSG_FILE_TYPE_MISMATCH      (GLFS_DHT_BASE + 8)

/*!
 * @messageid 109009
 * @diagnosis The GFID of the file/directory is different on different subvolumes
 * @recommendedaction  None
 *
 */

#define DHT_MSG_GFID_MISMATCH           (GLFS_DHT_BASE + 9)

/*!
 * @messageid 109010
 * @diagnosis The GFID of the specified file/directory is NULL.
 * @recommendedaction  None
 *
 */

#define DHT_MSG_GFID_NULL               (GLFS_DHT_BASE + 10)

/*
 * @messageid 109011
 * @diagnosis
 * @recommendedaction None
 */

#define DHT_MSG_HASHED_SUBVOL_GET_FAILED   (GLFS_DHT_BASE + 11)

/*!
 * @messageid 109012
 * @diagnosis The Distributed Hash Table Translator could not be initiated as the
 *            system is out of memory.
 * @recommendedaction  None
 *
 */

#define DHT_MSG_INIT_FAILED             (GLFS_DHT_BASE + 12)

/*!
 * @messageid 109013
 * @diagnosis Invalid DHT configuration in the volfile
 * @recommendedaction  None
 *
 */

#define DHT_MSG_INVALID_CONFIGURATION   (GLFS_DHT_BASE + 13)

/*!
 * @messageid 109014
 * @diagnosis Invalid disk layout
 * @recommendedaction  None
 *
 */

#define DHT_MSG_INVALID_DISK_LAYOUT     (GLFS_DHT_BASE + 14)

/*!
 * @messageid 109015
 * @diagnosis Invalid DHT configuration option.
 * @recommendedaction
 *              1. Reset the option with a valid value using the volume
 *                set command.
 *              2. Restart the process that logged the message in the
 *                log file.
 *
 */

#define DHT_MSG_INVALID_OPTION          (GLFS_DHT_BASE + 15)

/*!
 * @messageid 109016
 * @diagnosis The fix layout operation failed
 * @recommendedaction  None
 *
 */

#define DHT_MSG_LAYOUT_FIX_FAILED       (GLFS_DHT_BASE + 16)

/*!
 * @messageid 109017
 * @diagnosis Layout merge failed
 * @recommendedaction  None
 *
 */

#define DHT_MSG_LAYOUT_MERGE_FAILED     (GLFS_DHT_BASE + 17)

/*!
 * @messageid 109018
 * @diagnosis The layout for the specified directory does not match
                that on the disk.
 * @recommendedaction  None
 *
 */

#define DHT_MSG_LAYOUT_MISMATCH         (GLFS_DHT_BASE + 18)

/*!
 * @messageid 109019
 * @diagnosis No layout is present for the specified file/directory
 * @recommendedaction  None
 *
 */

#define DHT_MSG_LAYOUT_NULL             (GLFS_DHT_BASE + 19)

/*!
 * @messageid 109020
 * @diagnosis Informational message: Migration of data from the cached
 *      subvolume to the hashed subvolume is complete
 * @recommendedaction  None
 *
 */

#define DHT_MSG_MIGRATE_DATA_COMPLETE   (GLFS_DHT_BASE + 20)

/*!
 * @messageid 109021
 * @diagnosis Migration of data failed during the rebalance operation
 *     \n Cause: Directories could not be read to identify the files for the
 *             migration process.
 * @recommendedaction
 *             The log message would indicate the reason for the failure and
 *             the corrective action depends on the specific error that is
 *             encountered. The error is one of the standard UNIX errors.
 *
 */

#define DHT_MSG_MIGRATE_DATA_FAILED     (GLFS_DHT_BASE + 21)

/*!
 * @messageid 109022
 * @diagnosis Informational message: The file was migrated successfully during
 *              the rebalance operation.
 * @recommendedaction  None
 *
 */

#define DHT_MSG_MIGRATE_FILE_COMPLETE   (GLFS_DHT_BASE + 22)

/*!
 * @messageid 109023
 * @diagnosis File migration failed during the rebalance operation
 *            \n Cause: Rebalance moves data from the cached subvolume to
 *            the hashed subvolume. Migrating a single file is a multi-step operation
 *            which involves opening, reading, and writing the data and metadata.
 *            Any failures in this multi-step operation can result in a file
 *            migration failure.
 * @recommendedaction  The log message would indicate the reason for the failure and the
 *              corrective action depends on the specific error that is encountered.
 *              The error is one of the standard UNIX errors.
 *
 */

#define DHT_MSG_MIGRATE_FILE_FAILED     (GLFS_DHT_BASE + 23)

/*!
 * @messageid 109024
 * @diagnosis Out of memory
 * @recommendedaction  None
 *
 */

#define DHT_MSG_NO_MEMORY               (GLFS_DHT_BASE + 24)

/*!
 * @messageid 109025
 * @diagnosis  The opendir() call failed on the specified directory
 *              \n Cause: When a directory is renamed, the Distribute Hash
 *              table translator checks whether the destination directory
 *              is empty. This message indicates that the opendir() call
 *              on the destination directory has failed.
 * @recommendedaction The log message would indicate the reason for the
 *              failure and the corrective action depends on the specific
 *              error that is encountered. The error is one of the standard
 *              UNIX errors.
 *
 */

#define DHT_MSG_OPENDIR_FAILED          (GLFS_DHT_BASE + 25)

/*!
 * @messageid 109026
 * @diagnosis The rebalance operation failed.
 * @recommendedaction Check the log file for details about the failure.
 *     Possible causes:
 *     - A subvolume is down: Restart the rebalance operation after
 *             bringing up all subvolumes.
 *
 */

#define DHT_MSG_REBALANCE_FAILED        (GLFS_DHT_BASE + 26)

/*!
 * @messageid 109027
 * @diagnosis Failed to start the rebalance process.
 * @recommendedaction Check the log file for details about the failure.
 *
 */

#define DHT_MSG_REBALANCE_START_FAILED  (GLFS_DHT_BASE + 27)

/*!
 * @messageid 109028
 * @diagnosis Informational message that indicates the status of the
 *            rebalance operation and details as to how many files were
 *            migrated, skipped, failed etc
 * @recommendedaction  None
 *
 */

#define DHT_MSG_REBALANCE_STATUS        (GLFS_DHT_BASE + 28)

/*!
 * @messageid 109029
 * @diagnosis The rebalance operation was aborted by the user.
 * @recommendedaction  None
 *
 */

#define DHT_MSG_REBALANCE_STOPPED       (GLFS_DHT_BASE + 29)

/*!
 * @messageid 109030
 * @diagnosis The file or directory could not be renamed
 * @recommendedaction   Ensure that all the subvolumes are
 *                      online and reachable and try renaming
 *                      the file or directory again.
 *
 */

#define DHT_MSG_RENAME_FAILED           (GLFS_DHT_BASE + 30)

/*!
 * @messageid 109031
 * @diagnosis Attributes could not be set for the specified file or
 *             directory.
 * @recommendedaction  None
 *
 */

#define DHT_MSG_SETATTR_FAILED          (GLFS_DHT_BASE + 31)

/*!
 * @messageid 109032
 * @diagnosis The specified subvolume is running out of file system inodes.
        If all subvolumes run out of inodes, then new files cannot be created.
 * @recommendedaction  Consider adding more nodes to the cluster if all subvolumes
 *        run out of inodes
 *
 */

#define DHT_MSG_SUBVOL_INSUFF_INODES    (GLFS_DHT_BASE + 32)

/*!
 * @messageid 109033
 * @diagnosis The specified subvolume is running out of disk space. If all
              subvolumes run out of space, new files cannot be created.
 * @recommendedaction  Consider adding more bricks to the cluster if all subvolumes
 *              run out of disk space.
 *
 */

#define DHT_MSG_SUBVOL_INSUFF_SPACE    (GLFS_DHT_BASE + 33)

/*!
 * @messageid 109034
 * @diagnosis Failed to unlink the specified file/directory
 * @recommendedaction  The log message would indicate the reason
              for the failure and the corrective action depends on
              the specific error that is encountered.
 */

#define DHT_MSG_UNLINK_FAILED           (GLFS_DHT_BASE + 34)



/*!
 * @messageid 109035
 * @diagnosis The layout information could not be set in the inode
 * @recommendedaction  None
 *
 */

#define DHT_MSG_LAYOUT_SET_FAILED       (GLFS_DHT_BASE + 35)

/*!
 * @messageid 109036
 * @diagnosis Informational message regarding layout range distribution
 *            for a directory across subvolumes
 * @recommendedaction None
 */

#define DHT_MSG_LOG_FIXED_LAYOUT        (GLFS_DHT_BASE + 36)

/*
 * @messageid 109037
 * @diagnosis Informational message regarding error in tier operation
 * @recommendedaction None
 */

#define DHT_MSG_LOG_TIER_ERROR          (GLFS_DHT_BASE + 37)

/*
 * @messageid 109038
 * @diagnosis Informational message regarding tier operation
 * @recommendedaction None
 */

#define DHT_MSG_LOG_TIER_STATUS         (GLFS_DHT_BASE + 38)

/*
 * @messageid 109039
 * @diagnosis
 * @recommendedaction None
 */

#define DHT_MSG_GET_XATTR_FAILED        (GLFS_DHT_BASE + 39)

/*
 * @messageid 109040
 * @diagnosis
 * @recommendedaction None
 */

#define DHT_MSG_FILE_LOOKUP_FAILED      (GLFS_DHT_BASE + 40)

/*
 * @messageid 109041
 * @diagnosis
 * @recommendedaction None
 */

#define DHT_MSG_OPEN_FD_FAILED          (GLFS_DHT_BASE + 41)

/*
 * @messageid 109042
 * @diagnosis
 * @recommendedaction None
 */

#define DHT_MSG_SET_INODE_CTX_FAILED    (GLFS_DHT_BASE + 42)

/*
 * @messageid 109043
 * @diagnosis
 * @recommendedaction None
 */

#define DHT_MSG_UNLOCKING_FAILED        (GLFS_DHT_BASE + 43)

/*
 * @messageid 109044
 * @diagnosis
 * @recommendedaction None
 */

#define DHT_MSG_DISK_LAYOUT_NULL        (GLFS_DHT_BASE + 44)

/*
 * @messageid 109045
 * @diagnosis
 * @recommendedaction None
 */

#define DHT_MSG_SUBVOL_INFO             (GLFS_DHT_BASE + 45)

/*
 * @messageid 109046
 * @diagnosis
 * @recommendedaction None
 */

#define DHT_MSG_CHUNK_SIZE_INFO         (GLFS_DHT_BASE + 46)

/*
 * @messageid 109047
 * @diagnosis
 * @recommendedaction None
 */

#define DHT_MSG_LAYOUT_FORM_FAILED      (GLFS_DHT_BASE + 47)

/*
 * @messageid 109048
 * @diagnosis
 * @recommendedaction None
 */

#define DHT_MSG_SUBVOL_ERROR            (GLFS_DHT_BASE + 48)

/*
 * @messageid 109049
 * @diagnosis
 * @recommendedaction None
 */

#define DHT_MSG_LAYOUT_SORT_FAILED      (GLFS_DHT_BASE + 49)

/*
 * @messageid 109050
 * @diagnosis
 * @recommendedaction None
 */

#define DHT_MSG_REGEX_INFO              (GLFS_DHT_BASE + 50)

/*
 * @messageid 109051
 * @diagnosis
 * @recommendedaction None
 */

#define DHT_MSG_FOPEN_FAILED            (GLFS_DHT_BASE + 51)

/*
 * @messageid 109052
 * @diagnosis
 * @recommendedaction None
 */

#define DHT_MSG_SET_HOSTNAME_FAILED     (GLFS_DHT_BASE + 52)

/*
 * @messageid 109053
 * @diagnosis
 * @recommendedaction None
 */

#define DHT_MSG_BRICK_ERROR             (GLFS_DHT_BASE + 53)

/*
 * @messageid 109054
 * @diagnosis
 * @recommendedaction None
 */

#define DHT_MSG_SYNCOP_FAILED           (GLFS_DHT_BASE + 54)

/*
 * @messageid 109055
 * @diagnosis
 * @recommendedaction None
 */

#define DHT_MSG_MIGRATE_INFO            (GLFS_DHT_BASE + 55)

/*
 * @messageid 109056
 * @diagnosis
 * @recommendedaction None
 */

#define DHT_MSG_SOCKET_ERROR            (GLFS_DHT_BASE + 56)

/*
 * @messageid 109057
 * @diagnosis
 * @recommendedaction None
 */

#define DHT_MSG_CREATE_FD_FAILED        (GLFS_DHT_BASE + 57)

/*
 * @messageid 109058
 * @diagnosis
 * @recommendedaction None
 */

#define DHT_MSG_READDIR_ERROR           (GLFS_DHT_BASE + 58)

/*
 * @messageid 109059
 * @diagnosis
 * @recommendedaction None
 */

#define DHT_MSG_CHILD_LOC_BUILD_FAILED  (GLFS_DHT_BASE + 59)

/*
 * @messageid 109060
 * @diagnosis
 * @recommendedaction None
 */

#define DHT_MSG_SET_SWITCH_PATTERN_ERROR    (GLFS_DHT_BASE + 60)

/*
 * @messageid 109061
 * @diagnosis
 * @recommendedaction None
 */

#define DHT_MSG_COMPUTE_HASH_FAILED     (GLFS_DHT_BASE + 61)

/*
 * @messageid 109062
 * @diagnosis
 * @recommendedaction None
 */

#define DHT_MSG_FIND_LAYOUT_ANOMALIES_ERROR     (GLFS_DHT_BASE + 62)

/*
 * @messageid 109063
 * @diagnosis
 * @recommendedaction None
 */

#define DHT_MSG_ANOMALIES_INFO          (GLFS_DHT_BASE + 63)

/*
 * @messageid 109064
 * @diagnosis
 * @recommendedaction None
 */

#define DHT_MSG_LAYOUT_INFO             (GLFS_DHT_BASE + 64)

/*
 * @messageid 109065
 * @diagnosis
 * @recommendedaction None
 */

#define DHT_MSG_INODE_LK_ERROR          (GLFS_DHT_BASE + 65)

/*
 * @messageid 109066
 * @diagnosis
 * @recommendedaction None
 */

#define DHT_MSG_RENAME_INFO             (GLFS_DHT_BASE + 66)

/*
 * @messageid 109067
 * @diagnosis
 * @recommendedaction None
 */

#define DHT_MSG_DATA_NULL               (GLFS_DHT_BASE + 67)

/*
 * @messageid 109068
 * @diagnosis
 * @recommendedaction None
 */

#define DHT_MSG_AGGREGATE_QUOTA_XATTR_FAILED   (GLFS_DHT_BASE + 68)

/*
 * @messageid 109069
 * @diagnosis
 * @recommendedaction None
 */

#define DHT_MSG_UNLINK_LOOKUP_INFO      (GLFS_DHT_BASE + 69)

/*
 * @messageid 109070
 * @diagnosis
 * @recommendedaction None
 */

#define DHT_MSG_LINK_FILE_LOOKUP_INFO   (GLFS_DHT_BASE + 70)

/*
 * @messageid 109071
 * @diagnosis
 * @recommendedaction None
 */

#define DHT_MSG_OPERATION_NOT_SUP       (GLFS_DHT_BASE + 71)

/*
 * @messageid 109072
 * @diagnosis
 * @recommendedaction None
 */

#define DHT_MSG_NOT_LINK_FILE_ERROR     (GLFS_DHT_BASE + 72)

/*
 * @messageid 109073
 * @diagnosis
 * @recommendedaction None
 */

#define DHT_MSG_CHILD_DOWN              (GLFS_DHT_BASE + 73)

/*
 * @messageid 109074
 * @diagnosis
 * @recommendedaction None
 */

#define DHT_MSG_UUID_PARSE_ERROR        (GLFS_DHT_BASE + 74)

/*
 * @messageid 109075
 * @diagnosis
 * @recommendedaction None
 */

#define DHT_MSG_GET_DISK_INFO_ERROR     (GLFS_DHT_BASE + 75)

/*
 * @messageid 109076
 * @diagnosis
 * @recommendedaction None
 */

#define DHT_MSG_INVALID_VALUE           (GLFS_DHT_BASE + 76)

/*
 * @messageid 109077
 * @diagnosis
 * @recommendedaction None
 */

#define DHT_MSG_SWITCH_PATTERN_INFO     (GLFS_DHT_BASE + 77)

/*
 * @messageid 109078
 * @diagnosis
 * @recommendedaction None
 */

#define DHT_MSG_SUBVOL_OP_FAILED        (GLFS_DHT_BASE + 78)

/*
 * @messageid 109079
 * @diagnosis
 * @recommendedaction None
 */

#define DHT_MSG_LAYOUT_PRESET_FAILED    (GLFS_DHT_BASE + 79)

/*
 * @messageid 109080
 * @diagnosis
 * @recommendedaction None
 */

#define DHT_MSG_INVALID_LINKFILE        (GLFS_DHT_BASE + 80)

/*
 * @messageid 109081
 * @diagnosis
 * @recommendedaction None
 */

#define DHT_MSG_FIX_LAYOUT_INFO         (GLFS_DHT_BASE + 81)

/*
 * @messageid 109082
 * @diagnosis
 * @recommendedaction None
 */

#define DHT_MSG_GET_HOSTNAME_FAILED     (GLFS_DHT_BASE + 82)

/*
 * @messageid 109083
 * @diagnosis
 * @recommendedaction None
 */

#define DHT_MSG_WRITE_FAILED            (GLFS_DHT_BASE + 83)

/*
 * @messageid 109084
 * @diagnosis
 * @recommendedaction None
 */

#define DHT_MSG_MIGRATE_HARDLINK_FILE_FAILED (GLFS_DHT_BASE + 84)

/*
 * @messageid 109085
 * @diagnosis
 * @recommendedaction None
 */

#define DHT_MSG_FSYNC_FAILED            (GLFS_DHT_BASE + 85)

/*
 * @messageid 109086
 * @diagnosis
 * @recommendedaction None
 */

#define DHT_MSG_SUBVOL_DECOMMISSION_INFO (GLFS_DHT_BASE + 86)

/*
 * @messageid 109087
 * @diagnosis
 * @recommendedaction None
 */

#define DHT_MSG_BRICK_QUERY_FAILED      (GLFS_DHT_BASE + 87)

/*
 * @messageid 109088
 * @diagnosis
 * @recommendedaction None
 */

#define DHT_MSG_SUBVOL_NO_LAYOUT_INFO   (GLFS_DHT_BASE + 88)

/*
 * @messageid 109089
 * @diagnosis
 * @recommendedaction None
 */

#define DHT_MSG_OPEN_FD_ON_DST_FAILED   (GLFS_DHT_BASE + 89)

/*
 * @messageid 109090
 * @diagnosis
 * @recommendedaction None
 */

#define DHT_MSG_SUBVOL_NOT_FOUND        (GLFS_DHT_BASE + 90)

/*
 * @messageid 109190
 * @diagnosis
 * @recommendedaction None
 */

#define DHT_MSG_FILE_LOOKUP_ON_DST_FAILED   (GLFS_DHT_BASE + 91)

/*
 * @messageid 109092
 * @diagnosis
 * @recommendedaction None
 */

#define DHT_MSG_DISK_LAYOUT_MISSING     (GLFS_DHT_BASE + 92)

/*
 * @messageid 109093
 * @diagnosis
 * @recommendedaction None
 */

#define DHT_MSG_DICT_GET_FAILED         (GLFS_DHT_BASE + 93)

/*
 * @messageid 109094
 * @diagnosis
 * @recommendedaction None
 */

#define DHT_MSG_REVALIDATE_CBK_INFO     (GLFS_DHT_BASE + 94)

/*
 * @messageid 109095
 * @diagnosis
 * @recommendedaction None
 */

#define DHT_MSG_UPGRADE_BRICKS         (GLFS_DHT_BASE + 95)

/*
 * @messageid 109096
 * @diagnosis
 * @recommendedaction None
 */

#define DHT_MSG_LK_ARRAY_INFO           (GLFS_DHT_BASE + 96)

/*
 * @messageid 109097
 * @diagnosis
 * @recommendedaction None
 */

#define DHT_MSG_RENAME_NOT_LOCAL        (GLFS_DHT_BASE + 97)

/*
 * @messageid 109098
 * @diagnosis
 * @recommendedaction None
 */

#define DHT_MSG_RECONFIGURE_INFO        (GLFS_DHT_BASE + 98)

/*
 * @messageid 109099
 * @diagnosis
 * @recommendedaction None
 */

#define DHT_MSG_INIT_LOCAL_SUBVOL_FAILED        (GLFS_DHT_BASE + 99)

/*
 * @messageid 109100
 * @diagnosis
 * @recommendedaction None
 */

#define DHT_MSG_SYS_CALL_GET_TIME_FAILED        (GLFS_DHT_BASE + 100)

/*
 * @messageid 109101
 * @diagnosis
 * @recommendedaction None
 */

#define DHT_MSG_NO_DISK_USAGE_STATUS    (GLFS_DHT_BASE + 101)

/*
 * @messageid 109102
 * @diagnosis
 * @recommendedaction None
 */

#define DHT_MSG_SUBVOL_DOWN_ERROR       (GLFS_DHT_BASE + 102)

/*
 * @messageid 109103
 * @diagnosis
 * @recommendedaction None
 */

#define DHT_MSG_REBAL_THROTTLE_INFO       (GLFS_DHT_BASE + 103)

/*
 * @messageid 109104
 * @diagnosis
 * @recommendedaction None
 */

#define DHT_MSG_COMMIT_HASH_INFO       (GLFS_DHT_BASE + 104)

/*
 * @messageid 109105
 * @diagnosis
 * @recommendedaction None
 */

#define DHT_MSG_REBAL_STRUCT_SET       (GLFS_DHT_BASE + 105)

/*
 * @messageid 109106
 * @diagnosis
 * @recommendedaction None
 */

#define DHT_MSG_HAS_MIGINFO             (GLFS_DHT_BASE + 106)

/*
 * @messageid 109107
 * @diagnosis
 * @recommendedaction None
 */

#define DHT_MSG_LOG_IPC_TIER_ERROR      (GLFS_DHT_BASE + 107)

/*
 * @messageid 109108
 * @diagnosis
 * @recommendedaction None
 */

#define DHT_MSG_TIER_PAUSED             (GLFS_DHT_BASE + 108)

/*
 * @messageid 109109
 * @diagnosis
 * @recommendedaction None
 */

#define DHT_MSG_TIER_RESUME             (GLFS_DHT_BASE + 109)


/* @messageid 109110
 * @diagnosis
 * @recommendedaction None
 */

#define DHT_MSG_SETTLE_HASH_FAILED       (GLFS_DHT_BASE + 110)

/*
 * @messageid 109111
 * @diagnosis
 * @recommendedaction None
 */

#define DHT_MSG_DEFRAG_PROCESS_DIR_FAILED    (GLFS_DHT_BASE + 111)

/*
 * @messageid 109112
 * @diagnosis
 * @recommendedaction None
 */

#define DHT_MSG_FD_CTX_SET_FAILED         (GLFS_DHT_BASE + 112)

/*
 * @messageid 109113
 * @diagnosis
 * @recommendedaction None
 */

#define DHT_MSG_STALE_LOOKUP                    (GLFS_DHT_BASE + 113)

/*
 * @messageid 109114
 * @diagnosis
 * @recommendedaction None
 */
#define DHT_MSG_PARENT_LAYOUT_CHANGED  (GLFS_DHT_BASE + 114)

/*
 * @messageid 109115
 * @diagnosis
 * @recommendedaction None
 */
#define DHT_MSG_LOCK_MIGRATION_FAILED  (GLFS_DHT_BASE + 115)

/*
 * @messageid 109116
 * @diagnosis
 * @recommendedaction None
 */
#define DHT_MSG_LOCK_INODE_UNREF_FAILED  (GLFS_DHT_BASE + 116)

/*
 * @messageid 109117
 * @diagnosis
 * @recommendedaction None
 */
#define DHT_MSG_ASPRINTF_FAILED         (GLFS_DHT_BASE + 117)

/*
 * @messageid 109118
 * @diagnosis
 * @recommendedaction None
 */
#define DHT_MSG_DIR_LOOKUP_FAILED          (GLFS_DHT_BASE + 118)

#define glfs_msg_end_x GLFS_MSGID_END, "Invalid: End of messages"
#endif /* _DHT_MESSAGES_H_ */
