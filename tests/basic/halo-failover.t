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

cleanup;

TEST glusterd
TEST pidof glusterd
TEST $CLI volume create $V0 replica 3 $H0:$B0/${V0}{0,1,2}
TEST $CLI volume set $V0 cluster.background-self-heal-count 0
TEST $CLI volume set $V0 cluster.shd-max-threads 1
TEST $CLI volume set $V0 cluster.halo-enabled True
TEST $CLI volume set $V0 cluster.halo-max-replicas 2
TEST $CLI volume set $V0 cluster.quorum-type fixed
TEST $CLI volume set $V0 cluster.quorum-count 2
TEST $CLI volume set $V0 cluster.heal-timeout 5
TEST $CLI volume set $V0 cluster.entry-self-heal on
TEST $CLI volume set $V0 cluster.data-self-heal on
TEST $CLI volume set $V0 cluster.metadata-self-heal on
TEST $CLI volume set $V0 cluster.self-heal-daemon on
TEST $CLI volume set $V0 cluster.eager-lock off
TEST $CLI volume set $V0 network.ping-timeout 20
TEST $CLI volume set $V0 cluster.choose-local off
TEST $CLI volume start $V0
TEST glusterfs --volfile-id=/$V0 --volfile-server=$H0 $M0 --attribute-timeout=0 --entry-timeout=0
cd $M0

# Write some data to the mount
dd if=/dev/urandom of=$M0/test bs=1k count=200 oflag=sync &> /dev/null &

sleep 0.5
# Kill the first brick, fail-over to 3rd
TEST kill_brick $V0 $H0 $B0/${V0}0

# Test the mount is still RW (i.e. quorum works)
TEST dd if=/dev/urandom of=$M0/test_rw bs=1M count=1

# Wait for the dd to finish
wait
sleep 3

# Calulate the MD5s
MD5_B0=$(md5sum $B0/${V0}0/test | cut -d' ' -f1)
MD5_B1=$(md5sum $B0/${V0}1/test | cut -d' ' -f1)
MD5_B2=$(md5sum $B0/${V0}2/test | cut -d' ' -f1)

# Verify they are the same
TEST [ "$MD5_B1" == "$MD5_B2" ]

# Verify the failed brick has a different MD5
TEST [ x"$MD5_B0" != x"$MD5_B1" ]

cleanup
