#!/bin/bash

. $(dirname $0)/../../include.rc
. $(dirname $0)/../../volume.rc
cleanup;

TEST glusterd;
TEST pidof glusterd

function test_write {
        dd of=$M0/a if=/dev/urandom bs=1k count=1 2>&1 > /dev/null
}

#Tests for quorum-type option for replica 2
TEST $CLI volume create $V0 replica 2 $H0:$B0/${V0}{1,2};
TEST $CLI volume set $V0 performance.quick-read off
TEST $CLI volume set $V0 performance.io-cache off
TEST $CLI volume set $V0 performance.write-behind off
TEST $CLI volume set $V0 performance.stat-prefetch off
TEST $CLI volume set $V0 performance.read-ahead off
TEST $CLI volume start $V0
TEST $GFS -s $H0 --volfile-id=$V0 --direct-io-mode=enable $M0;

touch $M0/a
echo abc > $M0/b

TEST ! $CLI volume set $V0 cluster.quorum-type ""
TEST $CLI volume set $V0 cluster.quorum-type fixed
EXPECT fixed volume_option $V0 cluster.quorum-type
TEST $CLI volume set $V0 cluster.quorum-count 2
TEST test_write
TEST kill_brick $V0 $H0 $B0/${V0}1
TEST ! test_write
EXPECT "abc" cat $M0/b
TEST $CLI volume set $V0 cluster.quorum-reads on
EXPECT_WITHIN $CONFIG_UPDATE_TIMEOUT "1" mount_get_option_value $M0 $V0-replicate-0 quorum-reads
TEST ! cat $M0/b
TEST $CLI volume reset $V0 cluster.quorum-reads

TEST $CLI volume set $V0 cluster.quorum-type auto
EXPECT auto volume_option $V0 cluster.quorum-type
TEST $CLI volume start $V0 force
EXPECT_WITHIN $PROCESS_UP_TIMEOUT "1" afr_child_up_status $V0 0
TEST test_write
TEST kill_brick $V0 $H0 $B0/${V0}1
TEST ! test_write
EXPECT "abc" cat $M0/b
TEST $CLI volume set $V0 cluster.quorum-reads on
EXPECT_WITHIN $CONFIG_UPDATE_TIMEOUT "1" mount_get_option_value $M0 $V0-replicate-0 quorum-reads
TEST ! cat $M0/b
TEST $CLI volume reset $V0 cluster.quorum-reads

TEST $CLI volume set $V0 cluster.quorum-type none
EXPECT none volume_option $V0 cluster.quorum-type
TEST test_write
#Default is 'none' for even number of bricks in replication
TEST $CLI volume reset $V0 cluster.quorum-type
TEST test_write
EXPECT "abc" cat $M0/b
TEST $CLI volume set $V0 cluster.quorum-reads on
EXPECT_WITHIN $CONFIG_UPDATE_TIMEOUT "1" mount_get_option_value $M0 $V0-replicate-0 quorum-reads
EXPECT "abc" cat $M0/b
TEST $CLI volume reset $V0 cluster.quorum-reads


cleanup;
TEST glusterd;
TEST pidof glusterd

#Tests for quorum-type option for replica 3
TEST $CLI volume create $V0 replica 3 $H0:$B0/${V0}{1,2,3};
TEST $CLI volume set $V0 performance.quick-read off
TEST $CLI volume set $V0 performance.io-cache off
TEST $CLI volume set $V0 performance.write-behind off
TEST $CLI volume set $V0 performance.stat-prefetch off
TEST $CLI volume set $V0 performance.read-ahead off
TEST $CLI volume start $V0
TEST $GFS -s $H0 --volfile-id=$V0 --direct-io-mode=enable $M0;

touch $M0/a
echo abc > $M0/b

TEST $CLI volume set $V0 cluster.quorum-type fixed
EXPECT fixed volume_option $V0 cluster.quorum-type
TEST $CLI volume set $V0 cluster.quorum-count 3
TEST test_write
TEST kill_brick $V0 $H0 $B0/${V0}1
TEST ! test_write
EXPECT "abc" cat $M0/b
TEST $CLI volume set $V0 cluster.quorum-reads on
EXPECT_WITHIN $CONFIG_UPDATE_TIMEOUT "1" mount_get_option_value $M0 $V0-replicate-0 quorum-reads
TEST ! cat $M0/b
TEST $CLI volume reset $V0 cluster.quorum-reads


TEST $CLI volume set $V0 cluster.quorum-type auto
EXPECT auto volume_option $V0 cluster.quorum-type
TEST test_write
TEST kill_brick $V0 $H0 $B0/${V0}3
TEST ! test_write
EXPECT "abc" cat $M0/b
TEST $CLI volume set $V0 cluster.quorum-reads on
EXPECT_WITHIN $CONFIG_UPDATE_TIMEOUT "1" mount_get_option_value $M0 $V0-replicate-0 quorum-reads
TEST ! cat $M0/b
TEST $CLI volume reset $V0 cluster.quorum-reads


TEST $CLI volume set $V0 cluster.quorum-type none
EXPECT none volume_option $V0 cluster.quorum-type
TEST test_write
#Default is 'auto' for odd number of bricks in replication
TEST $CLI volume reset $V0 cluster.quorum-type
EXPECT "^$" volume_option $V0 cluster.quorum-type
TEST ! test_write
TEST $CLI volume start $V0 force
EXPECT_WITHIN $PROCESS_UP_TIMEOUT "1" afr_child_up_status $V0 0
EXPECT_WITHIN $PROCESS_UP_TIMEOUT "1" afr_child_up_status $V0 2
TEST kill_brick $V0 $H0 $B0/${V0}3
TEST test_write
cleanup;
