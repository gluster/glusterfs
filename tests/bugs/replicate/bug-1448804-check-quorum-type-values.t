#!/bin/bash
. $(dirname $0)/../../include.rc
. $(dirname $0)/../../volume.rc
cleanup;

TEST glusterd
TEST pidof glusterd
TEST $CLI volume create $V0 replica 2 $H0:$B0/${V0}{0..1}
TEST $CLI volume start $V0
EXPECT 'Started' volinfo_field $V0 'Status'

TEST glusterfs --volfile-id=$V0 --volfile-server=$H0 $M0;


# Default quorum-type for replica 2 is none. quorum-count is zero but it is not displayed.
EXPECT_WITHIN $CONFIG_UPDATE_TIMEOUT "none" echo `cat $M0/.meta/graphs/active/$V0-replicate-0/private|grep quorum-type|awk '{print $3}'`
cat $M0/.meta/graphs/active/$V0-replicate-0/private|grep quorum-count
TEST [ $? -ne 0 ]

# Convert to replica-3.
TEST $CLI volume add-brick $V0 replica 3 $H0:$B0/${V0}2
EXPECT_WITHIN $PROCESS_UP_TIMEOUT "1" afr_child_up_status $V0 2

# Default quorum-type for replica 3 is auto. quorum-count is INT_MAX but it is not displayed.
EXPECT_WITHIN $CONFIG_UPDATE_TIMEOUT "auto" echo `cat $M0/.meta/graphs/active/$V0-replicate-0/private|grep quorum-type|awk '{print $3}'`
cat $M0/.meta/graphs/active/$V0-replicate-0/private|grep quorum-count
TEST [ $? -ne 0 ]

# Change the type to fixed.
TEST $CLI volume set $V0 cluster.quorum-type fixed
# We haven't set quorum-count yet, so it takes the default value of zero in reconfigure() and hence the quorum-type is displayed as none.
EXPECT_WITHIN $CONFIG_UPDATE_TIMEOUT "none" echo `cat $M0/.meta/graphs/active/$V0-replicate-0/private|grep quorum-type|awk '{print $3}'`
cat $M0/.meta/graphs/active/$V0-replicate-0/private|grep quorum-count
TEST [ $? -ne 0 ]

# set quorum-count and check.
TEST $CLI volume set $V0 cluster.quorum-count 1
EXPECT_WITHIN $CONFIG_UPDATE_TIMEOUT "fixed" echo `cat $M0/.meta/graphs/active/$V0-replicate-0/private|grep quorum-type|awk '{print $3}'`
EXPECT "1" echo `cat $M0/.meta/graphs/active/$V0-replicate-0/private|grep quorum-count|awk '{print $3}'`

# reset to default values.
TEST $CLI volume reset $V0 cluster.quorum-type
EXPECT_WITHIN $CONFIG_UPDATE_TIMEOUT "auto" echo `cat $M0/.meta/graphs/active/$V0-replicate-0/private|grep quorum-type|awk '{print $3}'`
cat $M0/.meta/graphs/active/$V0-replicate-0/private|grep quorum-count
TEST [ $? -ne 0 ]

cleanup;
