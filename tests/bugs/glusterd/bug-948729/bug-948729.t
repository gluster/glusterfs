#!/bin/bash

. $(dirname $0)/../../../include.rc
. $(dirname $0)/../../../volume.rc
. $(dirname $0)/../../../cluster.rc

function check_peers {
        $CLI_1 peer status | grep 'Peer in Cluster (Connected)' | wc -l
}

cleanup;

uuid1=`uuidgen`;
uuid2=`uuidgen`;
uuid3=`uuidgen`;

TEST launch_cluster 2;

TEST $CLI_1 peer probe $H2;

EXPECT_WITHIN $PROBE_TIMEOUT 1 check_peers;

B3=/d/backends/3

mkdir -p $B3

TEST truncate -s 16M $B1/brick1
TEST truncate -s 16M $B2/brick2
TEST truncate -s 16M $B3/brick3

TEST LD1=`SETUP_LOOP $B1/brick1`
TEST MKFS_LOOP $LD1
TEST LD2=`SETUP_LOOP $B2/brick2`
TEST MKFS_LOOP $LD2
TEST LD3=`SETUP_LOOP $B3/brick3`
TEST MKFS_LOOP $LD3

mkdir -p $B1/$V0 $B2/$V0 $B3/$V0

TEST MOUNT_LOOP $LD1 $B1/$V0
TEST MOUNT_LOOP $LD2 $B2/$V0
TEST MOUNT_LOOP $LD3 $B3/$V0

#Tests without options 'mode=script' and 'wignore'
cli1=$(echo $CLI1 | sed 's/ --mode=script//')
cli1=$(echo $cli1 | sed 's/ --wignore//')
#Case 0: Parent directory of the brick is absent
TEST ! $cli1 volume create $V0 $H1:$B1/$V0/nonexistent/b1 $H2:$B2/$V0/nonexistent/b2

#Case 1: File system root being used as brick directory
TEST ! $cli1 volume create $V0 $H1:$B1/$V0 $H2:$B2/$V0

#Case 2: Brick directory contains only one component
TEST ! $cli1 volume create $V0 $H1:/$uuid1 $H2:/$uuid2

#Case 3: Sub-directories of the backend FS being used as brick directory
TEST   $cli1 volume create $V0 $H1:$B1/$V0/brick1 $H2:$B2/$V0/brick2

#add-brick tests
TEST ! $cli1 volume add-brick $V0 $H1:$B3/$V0/nonexistent/b3
TEST ! $cli1 volume add-brick $V0 $H1:$B3/$V0
TEST ! $cli1 volume add-brick $V0 $H1:/$uuid3
TEST   $cli1 volume add-brick $V0 $H1:$B3/$V0/brick3

#####replace-brick tests
#FIX-ME: Replace-brick does not work currently in the newly introduced cluster
#####test framework.

$CLI1 volume stop $V0

UMOUNT_LOOP $B1/$V0
UMOUNT_LOOP $B2/$V0
UMOUNT_LOOP $B3/$V0

rm -f  $B1/brick1
rm -f  $B2/brick2
rm -f  $B3/brick3


cleanup;
