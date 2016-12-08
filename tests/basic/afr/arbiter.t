#!/bin/bash

. $(dirname $0)/../../include.rc
. $(dirname $0)/../../volume.rc
. $(dirname $0)/../../afr.rc
cleanup;

TEST glusterd;
TEST pidof glusterd

# Non arbiter replica 3 volumes should not have arbiter-count option enabled.
TEST mkdir -p $B0/${V0}{0,1,2}
TEST $CLI volume create $V0 replica 3 $H0:$B0/${V0}{0,1,2}
TEST $CLI volume start $V0
EXPECT 'Started' volinfo_field $V0 'Status'
EXPECT_WITHIN $PROCESS_UP_TIMEOUT "1" brick_up_status $V0 $H0 $B0/${V0}0
EXPECT_WITHIN $PROCESS_UP_TIMEOUT "1" brick_up_status $V0 $H0 $B0/${V0}1
EXPECT_WITHIN $PROCESS_UP_TIMEOUT "1" brick_up_status $V0 $H0 $B0/${V0}2
TEST $GFS --volfile-id=$V0 --volfile-server=$H0 $M0;
TEST ! stat $M0/.meta/graphs/active/$V0-replicate-0/options/arbiter-count
EXPECT_WITHIN $UMOUNT_TIMEOUT "Y" force_umount $M0
TEST $CLI volume stop $V0
TEST $CLI volume delete $V0

# Make sure we clean up *all the way* so we don't get "brick X is already part
# of a volume" errors.
cleanup;
TEST glusterd;
TEST pidof glusterd

# Create and mount a replica 3 arbiter volume.
TEST mkdir -p $B0/${V0}{0,1,2}
TEST $CLI volume create $V0 replica 3 arbiter 1 $H0:$B0/${V0}{0,1,2}
TEST $CLI volume set $V0 performance.write-behind off
TEST $CLI volume set $V0 performance.stat-prefetch off
TEST $CLI volume set $V0 cluster.self-heal-daemon off
TEST $CLI volume set $V0 cluster.metadata-self-heal off
TEST $CLI volume set $V0 cluster.data-self-heal off
TEST $CLI volume set $V0 cluster.entry-self-heal off
TEST $CLI volume start $V0
EXPECT 'Started' volinfo_field $V0 'Status'
EXPECT_WITHIN $PROCESS_UP_TIMEOUT "1" brick_up_status $V0 $H0 $B0/${V0}0
EXPECT_WITHIN $PROCESS_UP_TIMEOUT "1" brick_up_status $V0 $H0 $B0/${V0}1
EXPECT_WITHIN $PROCESS_UP_TIMEOUT "1" brick_up_status $V0 $H0 $B0/${V0}2
TEST $GFS --volfile-id=$V0 --volfile-server=$H0 $M0;
TEST stat $M0/.meta/graphs/active/$V0-replicate-0/options/arbiter-count
EXPECT "1" cat $M0/.meta/graphs/active/$V0-replicate-0/options/arbiter-count

# Write data and metadata
TEST `echo hello >> $M0/file`
TEST setfattr -n user.name -v value1  $M0/file

# Data I/O will fail if arbiter is the only source.
TEST kill_brick $V0 $H0 $B0/${V0}0
TEST `echo "B0 is down, B1 and B2 are sources" >> $M0/file`
TEST setfattr -n user.name -v value2  $M0/file
TEST $CLI volume start $V0 force
EXPECT_WITHIN $PROCESS_UP_TIMEOUT "1" afr_child_up_status $V0 0
TEST kill_brick $V0 $H0 $B0/${V0}1
echo "B2 is down, B3 is the only source, writes will fail" >> $M0/file
EXPECT_NOT "0" echo $?
TEST ! cat $M0/file
# Though metadata IO could have been served from arbiter, we do not allow it
# anymore as FOPS like getfattr could be overloaded to return iatt buffers for
# use by other translators.
TEST ! getfattr -n user.name $M0/file
TEST ! setfattr -n user.name -v value3 $M0/file

#shd should not data self-heal from arbiter to the sinks.
TEST $CLI volume set $V0 cluster.self-heal-daemon on
EXPECT_WITHIN $PROCESS_UP_TIMEOUT "Y" glustershd_up_status
EXPECT_WITHIN $CHILD_UP_TIMEOUT "1" afr_child_up_status_in_shd $V0 0
EXPECT_WITHIN $CHILD_UP_TIMEOUT "1" afr_child_up_status_in_shd $V0 2
$CLI volume heal $V0
EXPECT_WITHIN $HEAL_TIMEOUT '1' echo $(count_sh_entries $B0/$V0"1")
EXPECT_WITHIN $HEAL_TIMEOUT '1' echo $(count_sh_entries $B0/$V0"2")

TEST $CLI volume start $V0 force
EXPECT_WITHIN $PROCESS_UP_TIMEOUT "Y" glustershd_up_status
EXPECT_WITHIN $CHILD_UP_TIMEOUT "1" afr_child_up_status_in_shd $V0 0
EXPECT_WITHIN $CHILD_UP_TIMEOUT "1" afr_child_up_status_in_shd $V0 1
EXPECT_WITHIN $CHILD_UP_TIMEOUT "1" afr_child_up_status_in_shd $V0 2
EXPECT_WITHIN $PROCESS_UP_TIMEOUT "1" afr_child_up_status $V0 1
TEST $CLI volume heal $V0
EXPECT 0 get_pending_heal_count $V0

# I/O can resume again.
TEST cat $M0/file
TEST getfattr -n user.name $M0/file
TEST `echo append>> $M0/file`
EXPECT_WITHIN $UMOUNT_TIMEOUT "Y" force_umount $M0
cleanup
