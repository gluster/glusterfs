/*Copyright (c) 2013 Red Hat, Inc. <http://www.redhat.com>
  This file is part of GlusterFS.

  This file is licensed to you under your choice of the GNU Lesser
  General Public License, version 3 or any later version (LGPLv3 or
  later), or the GNU General Public License, version 2 (GPLv2), in all
  cases as published by the Free Software Foundation.
*/

#ifndef _DHT_MESSAGES_H_
#define _DHT_MESSAGES_H_

#ifndef _CONFIG_H
#define _CONFIG_H
#include "config.h"
#endif

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
#define GLFS_DHT_NUM_MESSAGES           36
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

#define DHT_MSG_CREATE_LINK_FAILED       (GLFS_DHT_BASE + 2)

/*!
 * @messageid 109003
 * @diagnosis The value could not be set for the specified key in
 *       the dictionary
 *
 * @recommendedaction  None
 *
 */

#define DHT_MSG_DICT_SET_FAILED       (GLFS_DHT_BASE + 3)

/*!
 * @messageid 109004
 * @diagnosis Directory attributes could not be healed
 * @recommendedaction  None
 *
 */

#define DHT_MSG_DIR_ATTR_HEAL_FAILED (GLFS_DHT_BASE + 4)

/*!
 * @messageid 109005
 * @diagnosis Self-heal failed for the specified directory
 * @recommendedaction  Ensure that all subvolumes are online
 *              and reachable and perform a lookup operation
 *              on the directory again.
 *
 */

#define DHT_MSG_DIR_SELFHEAL_FAILED  (GLFS_DHT_BASE + 5)

/*!
 * @messageid 109006
 * @diagnosis The extended attributes could not be healed for
 *            the specified directory on the specified subvolume
 *
 * @recommendedaction  None
 *
 */

#define DHT_MSG_DIR_SELFHEAL_XATTR_FAILED  (GLFS_DHT_BASE + 6)

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

#define DHT_MSG_FILE_ON_MULT_SUBVOL               (GLFS_DHT_BASE + 7)

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

#define DHT_MSG_FILE_TYPE_MISMATCH        (GLFS_DHT_BASE + 8)

/*!
 * @messageid 109009
 * @diagnosis The GFID of the file/directory is different on different subvolumes
 * @recommendedaction  None
 *
 */

#define DHT_MSG_GFID_MISMATCH        (GLFS_DHT_BASE + 9)

/*!
 * @messageid 109010
 * @diagnosis The GFID of the specified file/directory is NULL.
 * @recommendedaction  None
 *
 */

#define DHT_MSG_GFID_NULL        (GLFS_DHT_BASE + 10)

/*!
 * @messageid 109011
 * @diagnosis The hashed subvolume could not be found for the specified
 *              file/directory
 * @recommendedaction  None
 *
 */

#define DHT_MSG_HASHED_SUBVOL_GET_FAILED        (GLFS_DHT_BASE + 11)

/*!
 * @messageid 109012
 * @diagnosis The Distributed Hash Table Translator could not be initiated as the
 *            system is out of memory.
 * @recommendedaction  None
 *
 */

#define DHT_MSG_INIT_FAILED  (GLFS_DHT_BASE + 12)

/*!
 * @messageid 109013
 * @diagnosis Invalid DHT configuration in the volfile
 * @recommendedaction  None
 *
 */

#define DHT_MSG_INVALID_CONFIGURATION  (GLFS_DHT_BASE + 13)

/*!
 * @messageid 109014
 * @diagnosis Invalid disk layout
 * @recommendedaction  None
 *
 */

#define DHT_MSG_INVALID_DISK_LAYOUT  (GLFS_DHT_BASE + 14)

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

#define DHT_MSG_INVALID_OPTION  (GLFS_DHT_BASE + 15)

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

#define DHT_MSG_LAYOUT_MERGE_FAILED       (GLFS_DHT_BASE + 17)

/*!
 * @messageid 109018
 * @diagnosis The layout for the specified directory does not match
                that on the disk.
 * @recommendedaction  None
 *
 */

#define DHT_MSG_LAYOUT_MISMATCH       (GLFS_DHT_BASE + 18)

/*!
 * @messageid 109019
 * @diagnosis No layout is present for the specified file/directory
 * @recommendedaction  None
 *
 */

#define DHT_MSG_LAYOUT_NULL       (GLFS_DHT_BASE + 19)

/*!
 * @messageid 109020
 * @diagnosis Informational message: Migration of data from the cached
 *      subvolume to the hashed subvolume is complete
 * @recommendedaction  None
 *
 */

#define DHT_MSG_MIGRATE_DATA_COMPLETE     (GLFS_DHT_BASE + 20)

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

#define DHT_MSG_MIGRATE_FILE_COMPLETE     (GLFS_DHT_BASE + 22)

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

#define DHT_MSG_NO_MEMORY        (GLFS_DHT_BASE + 24)

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

#define DHT_MSG_OPENDIR_FAILED       (GLFS_DHT_BASE + 25)

/*!
 * @messageid 109026
 * @diagnosis The rebalance operation failed.
 * @recommendedaction Check the log file for details about the failure.
 *     Possible causes:
 *     - A subvolume is down: Restart the rebalance operation after
 *             bringing up all subvolumes.
 *
 */

#define DHT_MSG_REBALANCE_FAILED   (GLFS_DHT_BASE + 26)

/*!
 * @messageid 109027
 * @diagnosis Failed to start the rebalance process.
 * @recommendedaction Check the log file for details about the failure.
 *
 */

#define DHT_MSG_REBALANCE_START_FAILED     (GLFS_DHT_BASE + 27)

/*!
 * @messageid 109028
 * @diagnosis Informational message that indicates the status of the
 *            rebalance operation and details as to how many files were
 *            migrated, skipped, failed etc
 * @recommendedaction  None
 *
 */

#define DHT_MSG_REBALANCE_STATUS     (GLFS_DHT_BASE + 28)

/*!
 * @messageid 109029
 * @diagnosis The rebalance operation was aborted by the user.
 * @recommendedaction  None
 *
 */

#define DHT_MSG_REBALANCE_STOPPED     (GLFS_DHT_BASE + 29)

/*!
 * @messageid 109030
 * @diagnosis The file or directory could not be renamed
 * @recommendedaction   Ensure that all the subvolumes are
 *                      online and reachable and try renaming
 *                      the file or directory again.
 *
 */

#define DHT_MSG_RENAME_FAILED        (GLFS_DHT_BASE + 30)

/*!
 * @messageid 109031
 * @diagnosis Attributes could not be set for the specified file or
 *             directory.
 * @recommendedaction  None
 *
 */

#define DHT_MSG_SETATTR_FAILED       (GLFS_DHT_BASE + 31)

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
 * @recommendedaction  Consider adding more nodes to the cluster if all subvolumes
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

#define DHT_MSG_UNLINK_FAILED     (GLFS_DHT_BASE + 34)



/*!
 * @messageid 109035
 * @diagnosis The layout information could not be set in the inode
 * @recommendedaction  None
 *
 */

#define DHT_MSG_LAYOUT_SET_FAILED     (GLFS_DHT_BASE + 35)

/*!
 * @messageid 109036
 * @diagnosis Informational message regarding layout range distribution
 *            for a directory across subvolumes
 * @recommendedaction None
 */

#define DHT_MSG_LOG_FIXED_LAYOUT     (GLFS_DHT_BASE + 36)

/*------------*/
#define glfs_msg_end_x GLFS_MSGID_END, "Invalid: End of messages"


#endif /* _DHT_MESSAGES_H_ */




