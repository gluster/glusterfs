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
TEST $CLI volume set $V0 cluster.halo-failover-enabled on
TEST $CLI volume set $V0 cluster.halo-max-replicas 2
TEST $CLI volume set $V0 cluster.halo-min-samples 1
TEST $CLI volume set $V0 cluster.quorum-type fixed
TEST $CLI volume set $V0 cluster.quorum-count 2
TEST $CLI volume set $V0 cluster.heal-timeout 5
TEST $CLI volume set $V0 cluster.entry-self-heal on
TEST $CLI volume set $V0 cluster.data-self-heal on
TEST $CLI volume set $V0 cluster.metadata-self-heal on
TEST $CLI volume set $V0 cluster.self-heal-daemon off
TEST $CLI volume set $V0 cluster.eager-lock off
TEST $CLI volume set $V0 network.ping-timeout 20
TEST $CLI volume set $V0 cluster.choose-local off
TEST $CLI volume set $V0 diagnostics.client-log-level DEBUG
TEST $CLI volume set $V0 diagnostics.brick-log-level DEBUG
TEST $CLI volume set $V0 nfs.log-level DEBUG
TEST $CLI volume start $V0
TEST glusterfs --volfile-id=/$V0 --volfile-server=$H0 $M0 --attribute-timeout=0 --entry-timeout=0

# Make sure two children are up and one is down.
EXPECT_WITHIN 10 "2 1" halo_sum_child_states 3

# Write some data to the mount
TEST dd if=/dev/urandom of=$M0/test bs=1k count=200 conv=fsync

KILL_IDX=$(cat /var/log/glusterfs/$M0LOG | grep "halo state: UP" | tail -n1 | grep -Eo "Child [0-9]+" | grep -Eo "[0-9]+")
TEST [ -n "$KILL_IDX" ]
# NB: UP_CHILDREN is the set of children that should be up after we kill
# the brick indicated by KILL_IDX, *not* the set of children which are
# currently up!
UP_CHILDREN=($(echo "0 1 2" | sed "s/${KILL_IDX}//g"))
UP1_HAS_TEST="$(ls $B0/${V0}${UP_CHILDREN[0]}/test 2>/dev/null)"
UP2_HAS_TEST="$(ls $B0/${V0}${UP_CHILDREN[1]}/test 2>/dev/null)"
VICTIM_HAS_TEST="$(ls $B0/${V0}${KILL_IDX}/test 2>/dev/null)"

# The victim brick should have a copy of the file.
TEST [ -n "$VICTIM_HAS_TEST" ]

# Of the bricks which will remain standing, there should be only one
# brick which has the file called test.  If the both have the first
# test file, the test is invalid as all the bricks are up and the
# halo-max-replicas is not being honored; e.g. bug exists.
TEST [ $([ -z "$UP1_HAS_TEST" ]) = $([ -z "$UP2_HAS_TEST" ]) ]

echo "Failing child ${KILL_IDX}..."
TEST kill_brick $V0 $H0 $B0/${V0}${KILL_IDX}

# Test the mount is still RW (i.e. quorum works)
TEST dd if=/dev/urandom of=$M0/test_failover bs=1M count=1 conv=fsync

# Calulate the MD5s
MD5_UP1=$(md5sum $B0/${V0}${UP_CHILDREN[0]}/test_failover | cut -d' ' -f1)
MD5_UP2=$(md5sum $B0/${V0}${UP_CHILDREN[1]}/test_failover | cut -d' ' -f1)

# Verify the two up bricks have identical MD5s, if both are identical
# then we must have successfully failed-over to the brick which was
# previously proven to be down (via the ONLY_ONE test).
TEST [ "$MD5_UP1" == "$MD5_UP2" ]

cleanup
