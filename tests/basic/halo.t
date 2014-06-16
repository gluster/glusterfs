#!/bin/bash
#
# Test for the Halo geo-replication feature
#
# 1. Create volume w/ 3x replication w/ max-replicas = 2 for clients,
#    heal daemon is off to start.
# 2. Write some data
# 3. Verify at least one of the bricks did not receive the writes.
# 4. Turn the heal daemon on
# 5. Within 30 seconds the SHD should async heal the data over
#    to the 3rd brick.
#

. $(dirname $0)/../include.rc
. $(dirname $0)/../volume.rc

cleanup;

TEST glusterd
TEST pidof glusterd
TEST $CLI volume create $V0 replica 3 $H0:$B0/${V0}{0,1,2}
TEST $CLI volume set $V0 cluster.background-self-heal-count 0
TEST $CLI volume set $V0 cluster.shd-max-threads 1
TEST $CLI volume set $V0 cluster.halo-enabled True
TEST $CLI volume set $V0 cluster.halo-max-replicas 2
TEST $CLI volume set $V0 cluster.heal-timeout 5
TEST $CLI volume set $V0 cluster.self-heal-daemon off
TEST $CLI volume set $V0 cluster.eager-lock off
TEST $CLI volume set $V0 cluster.choose-local off
TEST $CLI volume start $V0
TEST glusterfs --volfile-id=/$V0 --volfile-server=$H0 $M0 --attribute-timeout=0 --entry-timeout=0
cd $M0

for i in {1..5}
do
        dd if=/dev/urandom of=f bs=1M count=1 2>/dev/null
        mkdir a; cd a;
done

B0_CNT=$(ls $B0/${V0}0 | wc -l)
B1_CNT=$(ls $B0/${V0}1 | wc -l)
B2_CNT=$(ls $B0/${V0}2 | wc -l)

# One of the brick dirs should be empty
TEST "(($B0_CNT == 0 || $B1_CNT == 0 || $B2_CNT == 0))"

# Ok, turn the heal daemon on and verify it heals it up
TEST $CLI volume set $V0 cluster.self-heal-daemon on
EXPECT_WITHIN 30 "0" get_pending_heal_count $V0
cleanup
