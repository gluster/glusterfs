#!/bin/bash

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

TEST touch $M0/a.txt
TEST ls $B0/brick0/a.txt
TEST ls $B0/brick1/a.txt
TEST ! ls $B0/ta/a.txt

TEST dd if=/dev/zero of=$M0/a.txt bs=1M count=5

#Good Data brick is down. TA and bad brick are UP

TEST ta_kill_brick brick1
EXPECT_WITHIN $PROCESS_DOWN_TIMEOUT "0" ta_mount_child_up_status $M0 $V0 1
TEST dd if=/dev/zero of=$M0/a.txt bs=1M count=5
TEST ta_kill_brick brick0
EXPECT_WITHIN $PROCESS_DOWN_TIMEOUT "0"  ta_mount_child_up_status $M0 $V0 0
TEST ta_start_brick_process brick1
EXPECT_WITHIN $CHILD_UP_TIMEOUT "1" ta_mount_child_up_status $M0 $V0 1
TEST ! dd if=/dev/zero of=$M0/a.txt bs=1M count=5

# Good Data brick is UP. Bad and TA are down
TEST ta_kill_brick brick1
EXPECT_WITHIN $PROCESS_DOWN_TIMEOUT "0"  ta_mount_child_up_status $M0 $V0 1
TEST ta_start_brick_process brick0
EXPECT_WITHIN $CHILD_UP_TIMEOUT "1" ta_mount_child_up_status $M0 $V0 0
TEST ta_kill_brick ta
TEST ! dd if=/dev/zero of=$M0/a.txt bs=1M count=5

# Good and Bad data bricks are UP. TA is down
TEST ta_start_brick_process brick1
EXPECT_WITHIN $PROCESS_UP_TIMEOUT "1" ta_mount_child_up_status $M0 $V0 1
EXPECT_WITHIN $PROCESS_UP_TIMEOUT "1" ta_mount_child_up_status $M0 $V0 0
TEST dd if=/dev/zero of=$M0/a.txt bs=1M count=5

cleanup;
