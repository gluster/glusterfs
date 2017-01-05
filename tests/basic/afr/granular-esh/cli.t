#!/bin/bash

. $(dirname $0)/../../../include.rc
. $(dirname $0)/../../../volume.rc
. $(dirname $0)/../../../afr.rc

cleanup

TESTS_EXPECTED_IN_LOOP=4

TEST glusterd
TEST pidof glusterd

TEST   $CLI volume create $V0 replica 2 $H0:$B0/${V0}{0,1}
# Test that enabling the option should work on a newly created volume
TEST   $CLI volume set $V0 cluster.granular-entry-heal on
TEST   $CLI volume set $V0 cluster.granular-entry-heal off

#########################
##### DISPERSE TEST #####
#########################
# Execute the same command on a disperse volume and make sure it fails.
TEST $CLI volume create $V1 disperse 3 redundancy 1 $H0:$B0/${V1}{0,1,2}
TEST $CLI volume start $V1
TEST ! $CLI volume heal $V1 granular-entry-heal enable
TEST ! $CLI volume heal $V1 granular-entry-heal disable

#######################
###### TIER TEST ######
#######################
# Execute the same command on a disperse + replicate tiered volume and make
# sure the option is set on the replicate leg of the volume
TEST $CLI volume attach-tier $V1 replica 2 $H0:$B0/${V1}{3,4}
TEST $CLI volume heal $V1 granular-entry-heal enable
EXPECT "enable" volume_get_field $V1 cluster.granular-entry-heal
TEST $CLI volume heal $V1 granular-entry-heal disable
EXPECT "disable" volume_get_field $V1 cluster.granular-entry-heal

# Kill a disperse brick and make heal be pending on the volume.
TEST kill_brick $V1 $H0 $B0/${V1}0

# Now make sure that one offline brick in disperse does not affect enabling the
# option on the volume.
TEST $CLI volume heal $V1 granular-entry-heal enable
EXPECT "enable" volume_get_field $V1 cluster.granular-entry-heal
TEST $CLI volume heal $V1 granular-entry-heal disable
EXPECT "disable" volume_get_field $V1 cluster.granular-entry-heal

# Now kill a replicate brick.
TEST kill_brick $V1 $H0 $B0/${V1}3
# Now make sure that one offline brick in replicate causes the command to be
# failed.
TEST ! $CLI volume heal $V1 granular-entry-heal enable
EXPECT "disable" volume_get_field $V1 cluster.granular-entry-heal

######################
### REPLICATE TEST ###
######################
TEST   $CLI volume start $V0
TEST   $CLI volume set $V0 cluster.data-self-heal off
TEST   $CLI volume set $V0 cluster.metadata-self-heal off
TEST   $CLI volume set $V0 cluster.entry-self-heal off
TEST   $CLI volume set $V0 self-heal-daemon off
# Test that the volume-set way of enabling the option is disallowed
TEST ! $CLI volume set $V0 granular-entry-heal on
# Test that the volume-heal way of enabling the option is allowed
TEST   $CLI volume heal $V0 granular-entry-heal enable
# Volume-reset of the option should be allowed
TEST   $CLI volume reset $V0 granular-entry-heal
TEST   $CLI volume heal $V0 granular-entry-heal enable

EXPECT "enable" volume_option $V0 cluster.granular-entry-heal

TEST glusterfs --volfile-id=$V0 --volfile-server=$H0 $M0

# Kill brick-0.
TEST kill_brick $V0 $H0 $B0/${V0}0

# Disabling the option should work even when one or more bricks are down
TEST $CLI volume heal $V0 granular-entry-heal disable
# When a brick is down, 'enable' attempt should be failed
TEST ! $CLI volume heal $V0 granular-entry-heal enable

# Restart the killed brick
TEST $CLI volume start $V0 force
EXPECT_WITHIN $PROCESS_UP_TIMEOUT "1" afr_child_up_status $V0 0
EXPECT_WITHIN $PROCESS_UP_TIMEOUT "1" afr_child_up_status $V0 1

# When all bricks are up, it should be possible to enable the option
TEST $CLI volume heal $V0 granular-entry-heal enable

# Kill brick-0 again
TEST kill_brick $V0 $H0 $B0/${V0}0

# Create files under root
for i in {1..2}
do
        echo $i > $M0/f$i
done

# Test that the index associated with '/' is created on B1.
TEST stat $B0/${V0}1/.glusterfs/indices/entry-changes/$ROOT_GFID

# Check for successful creation of granular entry indices
for i in {1..2}
do
        TEST_IN_LOOP stat $B0/${V0}1/.glusterfs/indices/entry-changes/$ROOT_GFID/f$i
done

TEST $CLI volume start $V0 force
EXPECT_WITHIN $PROCESS_UP_TIMEOUT "1" afr_child_up_status $V0 0
EXPECT_WITHIN $PROCESS_UP_TIMEOUT "1" afr_child_up_status $V0 1

TEST gluster volume set $V0 cluster.self-heal-daemon on
EXPECT_WITHIN $PROCESS_UP_TIMEOUT "Y" glustershd_up_status
EXPECT_WITHIN $CHILD_UP_TIMEOUT "1" afr_child_up_status_in_shd $V0 0
EXPECT_WITHIN $CHILD_UP_TIMEOUT "1" afr_child_up_status_in_shd $V0 1

TEST $CLI volume heal $V0

# Wait for heal to complete
EXPECT_WITHIN $HEAL_TIMEOUT "^0$" get_pending_heal_count $V0

# Test if data was healed
for i in {1..2}
do
        TEST_IN_LOOP diff $B0/${V0}0/f$i $B0/${V0}1/f$i
done

# Now verify that there are no name indices left after self-heal
TEST ! stat $B0/${V0}1/.glusterfs/indices/entry-changes/$ROOT_GFID/f1
TEST ! stat $B0/${V0}1/.glusterfs/indices/entry-changes/$ROOT_GFID/f2
TEST ! stat $B0/${V0}1/.glusterfs/indices/entry-changes/$ROOT_GFID

# Perform a volume-reset-all-options operation
TEST $CLI volume reset $V0
# Ensure that granular entry heal is also disabled
EXPECT "no" volume_get_field $V0 cluster.granular-entry-heal
EXPECT "on" volume_get_field $V0 cluster.entry-self-heal

cleanup
#G_TESTDEF_TEST_STATUS_NETBSD7=BAD_TEST,BUG=1399038
