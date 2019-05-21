#!/bin/bash
#Self-heal tests

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
TEST ta_start_mount_process $M0
EXPECT_WITHIN $PROCESS_UP_TIMEOUT "1" ta_up_status $V0 $M0 0
EXPECT_WITHIN $PROCESS_UP_TIMEOUT "trusted.afr.patchy-ta-2" ls $B0/ta

TEST ta_create_shd_volfile brick0 brick1 ta
TEST ta_start_shd_process glustershd

TEST touch $M0/a.txt
TEST ta_kill_brick brick0
EXPECT_WITHIN $PROCESS_DOWN_TIMEOUT "0" ta_mount_child_up_status $M0 $V0 0
echo "Hello" >> $M0/a.txt
EXPECT "000000010000000000000000" get_hex_xattr trusted.afr.$V0-client-0 $B0/brick1/a.txt
EXPECT "000000010000000000000000" get_hex_xattr trusted.afr.$V0-client-0 $B0/ta/trusted.afr.$V0-ta-2

#TODO: After the write txn changes are merged, take statedump of TA process and
#check whether AFR_TA_DOM_NOTIFY lock is held by the client here. Take the
#statedump again after line #38 to check AFR_TA_DOM_NOTIFY lock is released by
#the SHD process.

TEST ta_start_brick_process brick0
EXPECT_WITHIN $CHILD_UP_TIMEOUT "1" ta_mount_child_up_status $M0 $V0 0
EXPECT_WITHIN $HEAL_TIMEOUT "000000000000000000000000" get_hex_xattr trusted.afr.$V0-client-0 $B0/brick1/a.txt
EXPECT_WITHIN $HEAL_TIMEOUT "000000000000000000000000" get_hex_xattr trusted.afr.$V0-client-0 $B0/ta/trusted.afr.$V0-ta-2

#Kill the previously up brick and try reading from other brick. Since the heal
#has happened file content should be same.
TEST ta_kill_brick brick1
EXPECT_WITHIN $PROCESS_DOWN_TIMEOUT "0" ta_mount_child_up_status $M0 $V0 1
#Umount and mount to remove cached data.
TEST umount $M0
TEST ta_start_mount_process $M0
EXPECT_WITHIN $PROCESS_UP_TIMEOUT "1" ta_up_status $V0 $M0 0
EXPECT "Hello" cat $M0/a.txt
cleanup;
