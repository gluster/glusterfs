#!/bin/bash
. $(dirname $0)/../../include.rc
. $(dirname $0)/../../volume.rc
cleanup;

TEST glusterd
TEST pidof glusterd
TEST $CLI volume create $V0 replica 3 $H0:$B0/${V0}{0..2}
TEST $CLI volume set $V0 self-heal-daemon off
TEST $CLI volume set $V0 cluster.data-self-heal off
TEST $CLI volume set $V0 cluster.metadata-self-heal off
TEST $CLI volume set $V0 cluster.entry-self-heal off
TEST $CLI volume set $V0 performance.write-behind off

TEST $CLI volume start $V0
TEST $GFS --volfile-id=$V0 --volfile-server=$H0 $M0;
TEST mkdir $M0/dir
TEST "echo abc > $M0/file1"
TEST "echo uvw > $M0/file2"

TEST kill_brick $V0 $H0 $B0/${V0}0
TEST "echo def > $M0/file1"
TEST "echo xyz > $M0/file2"

TEST $CLI volume start $V0 force
EXPECT_WITHIN $PROCESS_UP_TIMEOUT "1" brick_up_status $V0 $H0 $B0/${V0}0
EXPECT_WITHIN $PROCESS_UP_TIMEOUT "1" afr_child_up_status $V0 0

TEST kill_brick $V0 $H0 $B0/${V0}1

# Rename file1 and read it. Read should be served from the 3rd brick
TEST mv $M0/file1 $M0/file3
EXPECT "def" cat $M0/file3

# Create a link to file2 and read it. Read should be served from the 3rd brick
TEST ln $M0/file2 $M0/dir/file4
EXPECT "xyz" cat $M0/dir/file4
EXPECT "xyz" cat $M0/file2

cleanup
