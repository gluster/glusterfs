#!/bin/bash

. $(dirname $0)/../../include.rc
. $(dirname $0)/../../volume.rc
. $(dirname $0)/../../traps.rc
. $(dirname $0)/../../snapshot.rc
. $(dirname $0)/../../ssl.rc

function file_exists
{
        if [ -f $1 ]; then echo "Y"; else echo "N"; fi
}

function volume_online_brick_count
{
        $CLI volume status $V0 | awk '$1 == "Brick" &&  $6 != "N/A" { print $6}' | wc -l;
}

function total_online_bricks
{
        # This will count snapd, which isn't really a brick, but callers can
        # account for that so it's OK.
        find $GLUSTERD_WORKDIR -name '*.pid' | wc -l
}

cleanup;

# Initialize the test setup
TEST setup_lvm 1;

TEST create_self_signed_certs

# Start glusterd
TEST glusterd
TEST pidof glusterd;
#EST $CLI volume set all cluster.brick-multiplex on

# Create and start the volume
TEST $CLI volume create $V0 $H0:$L1/b1;

TEST $CLI volume start $V0;
EXPECT_WITHIN $CHILD_UP_TIMEOUT "1" volume_online_brick_count
EXPECT_WITHIN $PROCESS_UP_TIMEOUT "1" total_online_bricks

# Mount the volume and create some files
TEST $GFS --volfile-server=$H0 --volfile-id=$V0 $M0;

TEST touch $M0/file;

# Enable activate-on-create
TEST $CLI snapshot config activate-on-create enable;

# Create a snapshot
TEST $CLI snapshot create snap1 $V0 no-timestamp;
EXPECT_WITHIN $PROCESS_UP_TIMEOUT "2" total_online_bricks

TEST $CLI volume set $V0 features.uss enable;
EXPECT_WITHIN $PROCESS_UP_TIMEOUT "3" total_online_bricks
EXPECT_WITHIN $PROCESS_UP_TIMEOUT 'Y' check_if_snapd_exist

EXPECT_WITHIN $PROCESS_UP_TIMEOUT "Y" file_exists $M0/file
# Volume set can trigger graph switch therefore chances are we send this
# req to old graph. Old graph will not have .snaps. Therefore we should
# wait for some time.
EXPECT_WITHIN $PROCESS_UP_TIMEOUT "Y" file_exists $M0/.snaps/snap1/file

EXPECT_WITHIN $UMOUNT_TIMEOUT "Y" force_umount $M0

# Enable management encryption
touch  $GLUSTERD_WORKDIR/secure-access
killall_gluster

TEST glusterd
TEST pidof glusterd;
EXPECT_WITHIN $CHILD_UP_TIMEOUT "1" volume_online_brick_count
EXPECT_WITHIN $PROCESS_UP_TIMEOUT "3" total_online_bricks
EXPECT_WITHIN $PROCESS_UP_TIMEOUT 'Y' check_if_snapd_exist

# Mount the volume
TEST $GFS --volfile-server=$H0 --volfile-id=$V0 $M0;

EXPECT_WITHIN $PROCESS_UP_TIMEOUT "Y" file_exists $M0/file
EXPECT_WITHIN $PROCESS_UP_TIMEOUT "Y" file_exists $M0/.snaps/snap1/file

EXPECT_WITHIN $UMOUNT_TIMEOUT "Y" force_umount $M0

# Enable I/O encryption
TEST $CLI volume set $V0 client.ssl on
TEST $CLI volume set $V0 server.ssl on

killall_gluster

TEST glusterd
EXPECT_WITHIN $CHILD_UP_TIMEOUT "1" volume_online_brick_count
EXPECT_WITHIN $PROCESS_UP_TIMEOUT "3" total_online_bricks
EXPECT_WITHIN $PROCESS_UP_TIMEOUT 'Y' check_if_snapd_exist

# Mount the volume
TEST $GFS --volfile-server=$H0 --volfile-id=$V0 $M0;

EXPECT_WITHIN $PROCESS_UP_TIMEOUT "Y" file_exists $M0/file
EXPECT_WITHIN $PROCESS_UP_TIMEOUT "Y" file_exists $M0/.snaps/snap1/file

TEST $CLI snapshot delete all
TEST $CLI volume stop $V0
TEST $CLI volume delete $V0

cleanup;
