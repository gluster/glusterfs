#!/bin/bash

. $(dirname $0)/../../include.rc
. $(dirname $0)/../../cluster.rc

function check_peers {
        $CLI_1 peer status | grep 'Peer in Cluster (Connected)' | wc -l
}

cleanup;

uuid1=`uuidgen`;
uuid2=`uuidgen`;
uuid3=`uuidgen`;

V1=patchy1
V2=patchy2
V3=patchy3

TEST launch_cluster 2;

TEST $CLI_1 peer probe $H2;

EXPECT_WITHIN 20 1 check_peers;

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

TEST LD1=`losetup --find --show $B1/brick1`
TEST mkfs.xfs $LD1
TEST LD2=`losetup --find --show $B2/brick2`
TEST mkfs.xfs $LD2
TEST LD3=`losetup --find --show $B3/brick3`
TEST mkfs.xfs $LD3
TEST LD4=`losetup --find --show $B4/brick4`
TEST mkfs.xfs $LD4
TEST LD5=`losetup --find --show $B5/brick5`
TEST mkfs.xfs $LD5
TEST LD6=`losetup --find --show $B6/brick6`
TEST mkfs.xfs $LD6

mkdir -p $B1/$V0 $B2/$V0 $B3/$V0 $B4/$V0 $B5/$V0 $B6/$V0

TEST mount -t xfs $LD1 $B1/$V0
TEST mount -t xfs $LD2 $B2/$V0
TEST mount -t xfs $LD3 $B3/$V0
TEST mount -t xfs $LD4 $B4/$V0
TEST mount -t xfs $LD5 $B5/$V0
TEST mount -t xfs $LD6 $B6/$V0

#Case 0: Parent directory of the brick is absent
TEST ! $CLI_1 volume create $V0 $H1:$B1/$V0/nonexistent/b1 $H2:$B2/$V0/nonexistent/b2

#Case 1: File system root being used as brick directory
TEST   $CLI_1 volume create $V0 $H1:$B5/$V0 $H2:$B6/$V0

#Case 2: Brick directory contains only one component
TEST   $CLI_1 volume create $V1 $H1:/$uuid1 $H2:/$uuid2

#Case 3: Sub-directories of the backend FS being used as brick directory
TEST   $CLI_1 volume create $V2 $H1:$B1/$V0/brick1 $H2:$B2/$V0/brick2

#add-brick tests
TEST ! $CLI_1 volume add-brick $V0 $H1:$B3/$V0/nonexistent/brick3
TEST   $CLI_1 volume add-brick $V0 $H1:$B3/$V0
TEST   $CLI_1 volume add-brick $V1 $H1:/$uuid3
TEST   $CLI_1 volume add-brick $V2 $H1:$B4/$V0/brick3

#####replace-brick tests
#FIX-ME : replace-brick does not currently work in the newly introduced
#####cluster test framework

rmdir /$uuid1 /$uuid2 /$uuid3

cleanup;
