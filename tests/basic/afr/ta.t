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

TEST touch $M0/a.txt
TEST ls $B0/brick0/a.txt
TEST ls $B0/brick1/a.txt
TEST ! ls $B0/ta/a.txt

TEST ta_kill_brick brick0
EXPECT_WITHIN $PROCESS_DOWN_TIMEOUT "0" afr_child_up_status_meta $M0 $V0-replicate-0 0
TEST touch $M0/b.txt
EXPECT "000000000000000000000001" get_hex_xattr trusted.afr.$V0-client-0 $B0/brick1
EXPECT "000000010000000200000000" get_hex_xattr trusted.afr.$V0-client-0 $B0/brick1/b.txt
#New entry mark lead to pending data on the file and on ta
EXPECT "000000010000000100000000" get_hex_xattr trusted.afr.$V0-client-0 $B0/ta/trusted.afr.patchy-ta-2
TEST ! ls $B0/brick0/b.txt
TEST ls $B0/brick1/b.txt

#Try to create an entry while good brick is down and bad brick is UP. Should not create
TEST ta_start_brick_process brick0
EXPECT_WITHIN $CHILD_UP_TIMEOUT "1" afr_child_up_status_meta $M0 $V0-replicate-0 0
TEST ta_kill_brick brick1
EXPECT_WITHIN $PROCESS_DOWN_TIMEOUT "0" afr_child_up_status_meta $M0 $V0-replicate-0 1
TEST ! touch $M0/d.txt
EXPECT "000000010000000100000000" get_hex_xattr trusted.afr.$V0-client-0 $B0/ta/trusted.afr.patchy-ta-2

TEST ta_start_brick_process brick1
EXPECT_WITHIN $CHILD_UP_TIMEOUT "1" afr_child_up_status_meta $M0 $V0-replicate-0 1
TEST ta_kill_brick brick0
EXPECT_WITHIN $PROCESS_DOWN_TIMEOUT "0" afr_child_up_status_meta $M0 $V0-replicate-0 0

TEST ta_kill_brick ta
# Entry create must fail if only one brick is UP, even if that is a good brick.
TEST ! touch $M0/c.txt
TEST ! ls $B0/brick0/c.txt
TEST ! ls $B0/brick1/c.txt

cleanup;
