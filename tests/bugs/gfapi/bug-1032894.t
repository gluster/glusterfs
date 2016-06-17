#!/bin/bash

. $(dirname $0)/../../include.rc
. $(dirname $0)/../../volume.rc

#Check stale indices are deleted as part of self-heal-daemon crawl.
cleanup;

TEST glusterd
TEST pidof glusterd
TEST $CLI volume create $V0 replica 2 $H0:$B0/${V0}{0,1}
TEST $CLI volume set $V0 cluster.self-heal-daemon off
TEST $CLI volume start $V0
TEST glusterfs --volfile-id=/$V0 --volfile-server=$H0 $M0
cd $M0
TEST mkdir a
cd a
TEST kill_brick $V0 $H0 $B0/${V0}0
# Create stale indices
for i in {1..10}; do echo abc > $i; done
for i in {1..10}; do rm -f $i; done

TEST $CLI volume start $V0 force
EXPECT_WITHIN $CHILD_UP_TIMEOUT "1" afr_child_up_status_in_shd $V0 0
TEST $CLI volume set $V0 cluster.self-heal-daemon on
EXPECT_WITHIN $PROCESS_UP_TIMEOUT "Y" glustershd_up_status

#Since maximum depth of the directory structure that needs healin is 2
#Trigger two self-heals. That should make sure the heal is complete
TEST $CLI volume heal $V0

EXPECT_WITHIN $HEAL_TIMEOUT "0"  afr_get_index_count $B0/${V0}1
cleanup
