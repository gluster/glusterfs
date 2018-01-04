#!/bin/bash
. $(dirname $0)/../../include.rc
. $(dirname $0)/../../volume.rc

cleanup;

TEST glusterd
TEST pidof glusterd
TEST $CLI volume create $V0 replica 2 $H0:$B0/${V0}{1,2};
TEST $CLI volume start $V0
EXPECT_WITHIN $PROCESS_UP_TIMEOUT 'Started' volinfo_field $V0 'Status';

#bug-1102656 - validating volume top command

TEST $CLI volume top $V0 open
TEST ! $CLI volume top $V0 open brick $H0:/tmp/brick
TEST $CLI volume top $V0 read

TEST $CLI volume status

#bug- 1002556
EXPECT '1 x 2 = 2' volinfo_field $V0 'Number of Bricks';

TEST $CLI volume add-brick $V0 replica 3 $H0:$B0/${V0}3
EXPECT '1 x 3 = 3' volinfo_field $V0 'Number of Bricks';

TEST $CLI volume remove-brick $V0 replica 2 $H0:$B0/${V0}3 force
EXPECT '1 x 2 = 2' volinfo_field $V0 'Number of Bricks';

TEST killall glusterd
TEST glusterd

EXPECT '1 x 2 = 2' volinfo_field $V0 'Number of Bricks';

#bug-1406411- fail-add-brick-when-replica-count-changes

EXPECT_WITHIN $PROCESS_UP_TIMEOUT "1" brick_up_status $V0 $H0 $B0/${V0}1
EXPECT_WITHIN $PROCESS_UP_TIMEOUT "1" brick_up_status $V0 $H0 $B0/${V0}2
TEST kill_brick $V0 $H0 $B0/${V0}1

#add-brick should fail
TEST ! $CLI_NO_FORCE volume add-brick $V0 replica 3 $H0:$B0/${V0}3

TEST $CLI volume start $V0 force
EXPECT_WITHIN $PROCESS_UP_TIMEOUT "1" brick_up_status $V0 $H0 $B0/${V0}1
EXPECT_WITHIN $PROCESS_UP_TIMEOUT "1" brick_up_status $V0 $H0 $B0/${V0}2
TEST $CLI volume add-brick $V0 replica 3 $H0:$B0/${V0}3

TEST $CLI volume create $V1 $H0:$B0/${V1}{1,2};
TEST $CLI volume start $V1

EXPECT_WITHIN $PROCESS_UP_TIMEOUT "1" brick_up_status $V1 $H0 $B0/${V1}1
EXPECT_WITHIN $PROCESS_UP_TIMEOUT "1" brick_up_status $V1 $H0 $B0/${V1}2
TEST kill_brick $V1 $H0 $B0/${V1}1

#add-brick should fail
TEST ! $CLI_NO_FORCE volume add-brick $V1 replica 2 $H0:$B0/${V1}{3,4}

TEST $CLI volume start $V1 force
EXPECT_WITHIN $PROCESS_UP_TIMEOUT "1" brick_up_status $V1 $H0 $B0/${V1}1
EXPECT_WITHIN $PROCESS_UP_TIMEOUT "1" brick_up_status $V1 $H0 $B0/${V1}2

TEST $CLI volume add-brick $V1 replica 2 $H0:$B0/${V1}{3,4}

#bug-905307 - validate cluster.post-op-delay-secs option

#Strings should not be accepted.
TEST ! $CLI volume set $V0 cluster.post-op-delay-secs abc

#-ve ints should not be accepted.
TEST ! $CLI volume set $V0 cluster.post-op-delay-secs -1

#INT_MAX+1 should not be accepted.
TEST ! $CLI volume set $V0 cluster.post-op-delay-secs 2147483648

#floats should not be accepted.
TEST ! $CLI volume set $V0 cluster.post-op-delay-secs 1.25

#min val 0 should be accepted
TEST $CLI volume set $V0 cluster.post-op-delay-secs 0
EXPECT "0" volume_option $V0 cluster.post-op-delay-secs

#max val 2147483647 should be accepted
TEST $CLI volume set $V0 cluster.post-op-delay-secs 2147483647
EXPECT "2147483647" volume_option $V0 cluster.post-op-delay-secs

#some middle val in range 2147 should be accepted
TEST $CLI volume set $V0 cluster.post-op-delay-secs 2147
EXPECT "2147" volume_option $V0 cluster.post-op-delay-secs

#bug-1265479 - validate-replica-volume-options

#Setting data-self-heal option on for distribute-replicate volume
TEST $CLI volume set $V1 data-self-heal on
EXPECT 'on' volinfo_field $V1 'cluster.data-self-heal';
TEST $CLI volume set $V1 cluster.data-self-heal on
EXPECT 'on' volinfo_field $V1 'cluster.data-self-heal';

#Setting metadata-self-heal option on for distribute-replicate volume
TEST $CLI volume set $V1 metadata-self-heal on
EXPECT 'on' volinfo_field $V1 'cluster.metadata-self-heal';
TEST $CLI volume set $V1 cluster.metadata-self-heal on

#Setting entry-self-heal option on for distribute-replicate volume
TEST $CLI volume set $V1 entry-self-heal on
EXPECT 'on' volinfo_field $V1 'cluster.entry-self-heal';
TEST $CLI volume set $V1 cluster.entry-self-heal on
EXPECT 'on' volinfo_field $V1 'cluster.entry-self-heal';

cleanup
