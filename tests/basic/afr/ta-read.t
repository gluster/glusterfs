#!/bin/bash

# Test read transaction logic for thin-arbiter.

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

TEST touch $M0/FILE
TEST ls $B0/brick0/FILE
TEST ls $B0/brick1/FILE
TEST ! ls $B0/ta/FILE

# Kill one brick and write to FILE.
TEST ta_kill_brick brick0
EXPECT_WITHIN $PROCESS_DOWN_TIMEOUT "0" ta_mount_child_up_status $M0 $V0 0
echo "brick0 down">> $M0/FILE
TEST [ $? -eq 0 ]
EXPECT "000000010000000000000000" get_hex_xattr trusted.afr.$V0-client-0 $B0/brick1/FILE
EXPECT "000000010000000000000000" get_hex_xattr trusted.afr.$V0-client-0 $B0/ta/trusted.afr.patchy-ta-2

#Umount and mount to remove cached data.
EXPECT_WITHIN $UMOUNT_TIMEOUT "Y" force_umount $M0
TEST ta_start_mount_process $M0
EXPECT_WITHIN $PROCESS_UP_TIMEOUT "1" ta_up_status $V0 $M0 0
EXPECT_WITHIN $PROCESS_UP_TIMEOUT "1" ta_mount_child_up_status $M0 $V0 1
# Read must be allowed since good brick is up.
TEST  cat $M0/FILE

#Umount and mount to remove cached data.
EXPECT_WITHIN $UMOUNT_TIMEOUT "Y" force_umount $M0
TEST ta_start_mount_process $M0
EXPECT_WITHIN $PROCESS_UP_TIMEOUT "1" ta_up_status $V0 $M0 0
# Toggle good and bad data brick processes.
TEST ta_start_brick_process brick0
EXPECT_WITHIN $PROCESS_UP_TIMEOUT "1" ta_mount_child_up_status $M0 $V0 0
TEST ta_kill_brick brick1
EXPECT_WITHIN $PROCESS_DOWN_TIMEOUT "0" ta_mount_child_up_status $M0 $V0 1
# Read must now fail.
TEST ! cat $M0/FILE

# Bring all data bricks up, and kill TA.
TEST ta_start_brick_process brick1
EXPECT_WITHIN $PROCESS_UP_TIMEOUT "1" ta_mount_child_up_status $M0 $V0 1
TA_PID=$(ta_get_pid_by_brick_name ta)
TEST [ -n $TA_PID ]
TEST ta_kill_brick ta
TA_PID=$(ta_get_pid_by_brick_name ta)
TEST [ -z $TA_PID ]
# Read must now succeed.
TEST cat $M0/FILE
cleanup;
