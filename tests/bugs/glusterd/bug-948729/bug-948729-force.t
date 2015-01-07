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

V1=patchy1
V2=patchy2

TEST launch_cluster 2;

TEST $CLI_1 peer probe $H2;

EXPECT_WITHIN $PROBE_TIMEOUT 1 check_peers;

B3=/d/backends/3
B4=/d/backends/4
B5=/d/backends/5
B6=/d/backends/6

mkdir -p $B3 $B4 $B5 $B6

TEST truncate -s 16M $B1/brick1
TEST truncate -s 16M $B2/brick2
TEST truncate -s 16M $B3/brick3
TEST truncate -s 16M $B4/brick4
TEST truncate -s 16M $B5/brick5
TEST truncate -s 16M $B6/brick6

TEST LD1=`SETUP_LOOP $B1/brick1`
TEST MKFS_LOOP $LD1
TEST LD2=`SETUP_LOOP $B2/brick2`
TEST MKFS_LOOP $LD2
TEST LD3=`SETUP_LOOP $B3/brick3`
TEST MKFS_LOOP $LD3
TEST LD4=`SETUP_LOOP $B4/brick4`
TEST MKFS_LOOP $LD4
TEST LD5=`SETUP_LOOP $B5/brick5`
TEST MKFS_LOOP $LD5
TEST LD6=`SETUP_LOOP $B6/brick6`
TEST MKFS_LOOP $LD6

mkdir -p $B1/$V0 $B2/$V0 $B3/$V0 $B4/$V0 $B5/$V0 $B6/$V0

TEST MOUNT_LOOP $LD1 $B1/$V0
TEST MOUNT_LOOP $LD2 $B2/$V0
TEST MOUNT_LOOP $LD3 $B3/$V0
TEST MOUNT_LOOP $LD4 $B4/$V0
TEST MOUNT_LOOP $LD5 $B5/$V0
TEST MOUNT_LOOP $LD6 $B6/$V0

#Case 0: Parent directory of the brick is absent
TEST ! $CLI1 volume create $V0 $H1:$B1/$V0/nonexistent/b1 $H2:$B2/$V0/nonexistent/b2 force

#Case 1: File system root is being used as brick directory
TEST   $CLI1 volume create $V0 $H1:$B5/$V0 $H2:$B6/$V0 force

#Case 2: Brick directory contains only one component
TEST   $CLI1 volume create $V1 $H1:/$uuid1 $H2:/$uuid2 force

#Case 3: Sub-directories of the backend FS being used as brick directory
TEST   $CLI1 volume create $V2 $H1:$B1/$V0/brick1 $H2:$B2/$V0/brick2 force

#add-brick tests
TEST ! $CLI1 volume add-brick $V0 $H1:$B3/$V0/nonexistent/brick3 force
TEST   $CLI1 volume add-brick $V0 $H1:$B3/$V0 force
TEST   $CLI1 volume add-brick $V1 $H1:/$uuid3 force
TEST   $CLI1 volume add-brick $V2 $H1:$B4/$V0/brick3 force

#####replace-brick tests
#FIX-ME: replace-brick does not work with the newly introduced cluster test
#####framework

rmdir /$uuid1 /$uuid2 /$uuid3;

$CLI volume stop $V0
$CLI volume stop $V1
$CLI volume stop $V2

UMOUNT_LOOP $B1/$V0
UMOUNT_LOOP $B2/$V0
UMOUNT_LOOP $B3/$V0
UMOUNT_LOOP $B4/$V0
UMOUNT_LOOP $B5/$V0
UMOUNT_LOOP $B6/$V0

rm -f $B1/brick1
rm -f $B2/brick2
rm -f $B3/brick3
rm -f $B4/brick4
rm -f $B5/brick5
rm -f $B6/brick6

cleanup;
