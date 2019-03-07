#!/bin/bash

# Test read transaction inode refresh logic for thin-arbiter.

. $(dirname $0)/../../include.rc
. $(dirname $0)/../../volume.rc
. $(dirname $0)/../../thin-arbiter.rc
cleanup;
TEST ta_create_brick_and_volfile brick0
TEST ta_create_brick_and_volfile brick1
TEST ta_create_ta_and_volfile ta
TEST ta_start_brick_process brick0
TEST ta_start_brick_process brick1
TEST ta_start_ta_process ta

TEST ta_create_mount_volfile brick0 brick1 ta
# Set afr xlator options to choose brick0 as read-subvol.
sed -i '/iam-self-heal-daemon/a \     option read-subvolume-index 0' $B0/mount.vol
TEST [ $? -eq 0 ]
sed -i '/iam-self-heal-daemon/a \     option choose-local false' $B0/mount.vol
TEST [ $? -eq 0 ]

TEST ta_start_mount_process $M0
EXPECT_WITHIN $PROCESS_UP_TIMEOUT "1" ta_up_status $V0 $M0 0
EXPECT_WITHIN $PROCESS_UP_TIMEOUT "trusted.afr.patchy-ta-2" ls $B0/ta

TEST touch $M0/FILE
TEST ls $B0/brick0/FILE
TEST ls $B0/brick1/FILE
TEST ! ls $B0/ta/FILE
TEST setfattr -n user.name -v ravi $M0/FILE

# Remove gfid hardlink from brick0 which is the read-subvol for FILE.
# This triggers inode refresh up on a getfattr and eventually calls
# afr_ta_read_txn(). Without this patch, afr_ta_read_txn() will again query
# brick0 causing getfattr to fail.
TEST rm -f $(gf_get_gfid_backend_file_path $B0/brick0 FILE)
TEST getfattr -n user.name $M0/FILE

cleanup;
