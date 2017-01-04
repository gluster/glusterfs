#!/bin/bash
#
# Tests that fail-over works correctly for Halo Geo-replication
#
# 1. Create a volume @ 3x replication w/ halo + quorum enabled
# 2. Write some data, background it & fail a brick
# 3. The expected result is that the writes fail-over to the 3rd
#    brick immediatelly, and md5s will show they are equal once
#    the write completes.
# 4. The mount should also be RW after the brick is killed as
#    quorum will be immediately restored by swapping in the
#    other brick.
#
. $(dirname $0)/../include.rc
. $(dirname $0)/../volume.rc
. $(dirname $0)/../halo.rc

cleanup;

TEST glusterd
TEST pidof glusterd
TEST $CLI volume create $V0 replica 3 $H0:$B0/${V0}{0,1,2}
TEST $CLI volume set $V0 cluster.background-self-heal-count 0
TEST $CLI volume set $V0 cluster.shd-max-threads 1
TEST $CLI volume set $V0 cluster.halo-enabled True
TEST $CLI volume set $V0 cluster.halo-max-latency 9999
TEST $CLI volume set $V0 cluster.halo-shd-max-latency 9999
TEST $CLI volume set $V0 cluster.halo-max-replicas 2
TEST $CLI volume set $V0 cluster.halo-min-samples 1
TEST $CLI volume set $V0 cluster.halo-failover-enabled off
TEST $CLI volume set $V0 cluster.quorum-type fixed
TEST $CLI volume set $V0 cluster.quorum-count 2
TEST $CLI volume set $V0 cluster.heal-timeout 5
TEST $CLI volume set $V0 cluster.entry-self-heal on
TEST $CLI volume set $V0 cluster.data-self-heal on
TEST $CLI volume set $V0 cluster.metadata-self-heal on
TEST $CLI volume set $V0 cluster.self-heal-daemon off
TEST $CLI volume set $V0 cluster.eager-lock off
TEST $CLI volume set $V0 diagnostics.client-log-level DEBUG
TEST $CLI volume set $V0 diagnostics.brick-log-level DEBUG
TEST $CLI volume set $V0 nfs.log-level DEBUG

# Use a large ping time here so the spare brick is not marked up
# based on the ping time.  The only way it can get marked up is
# by being swapped in via the down event (which is what we are disabling).
TEST $CLI volume set $V0 network.ping-timeout 1000
TEST $CLI volume set $V0 cluster.choose-local off
TEST $CLI volume start $V0
TEST glusterfs --volfile-id=/$V0 --volfile-server=$H0 $M0 --attribute-timeout=0 --entry-timeout=0

# Make sure two children are up and one is down.
EXPECT_WITHIN 10 "2 1" halo_sum_child_states 3

# Write some data to the mount
TEST dd if=/dev/urandom of=$M0/test bs=1k count=200 conv=fsync

UP_IDX=$(cat /var/log/glusterfs/$M0LOG  | grep "halo state: UP" | tail -n1 | grep -Eo "Child [0-9]+" | grep -Eo "[0-9]+")
TEST kill_brick $V0 $H0 $B0/${V0}${UP_IDX}

# Make sure two children are down and one is up.
EXPECT_WITHIN 10 "1 2" halo_sum_child_states 3

# Test that quorum should fail and the mount is RO, the reason here
# is that although there _is_ another brick running which _could_
# take the failed bricks place, it is not marked "up" so quorum
# will not be fullfilled.  If we waited 1000 second the brick would
# indeed be activated based on ping time, but for our test we want
# the decision to be solely "down event" driven, not ping driven.
TEST ! dd if=/dev/urandom of=$M0/test_rw bs=1M count=1 conv=fsync

TEST $CLI volume start $V0 force
EXPECT_WITHIN $CHILD_UP_TIMEOUT "1" afr_child_up_status $V0 $UP_IDX

# Test that quorum should be restored and the file is writable
TEST dd if=/dev/urandom of=$M0/test_rw bs=1M count=1

cleanup
