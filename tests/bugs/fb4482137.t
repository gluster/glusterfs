#!/bin/bash

#
# Test the scenario where a SHD daemon suffers a frame timeout during a
# crawl.  The expected behavior is that present crawl will continue
# after the timeout and not deadlock.
#

. $(dirname $0)/../include.rc
. $(dirname $0)/../volume.rc

cleanup;

function wait_for_shd_no_sink() {
  local TIMEOUT=$1
  # If we see the "no active sinks" log message we know
  # the heal is alive.  It cannot proceed as the "sink"
  # is hung, but it's at least alive and trying.
  timeout $TIMEOUT grep -q 'replicate-0: no active sinks for' \
    <(tail -fn0 /var/log/glusterfs/glustershd.log)
  return $?
}

TEST glusterd
TEST pidof glusterd
TEST $CLI volume info 2> /dev/null;

# Setup a cluster with 3 replicas, and fav child by majority on
TEST $CLI volume create $V0 replica 3 $H0:$B0/${V0}{1..3};
TEST $CLI volume set $V0 network.frame-timeout 2
TEST $CLI volume set $V0 cluster.choose-local off
TEST $CLI volume set $V0 cluster.entry-self-heal off
TEST $CLI volume set $V0 cluster.metadata-self-heal off
TEST $CLI volume set $V0 cluster.data-self-heal off
TEST $CLI volume set $V0 cluster.self-heal-daemon on
TEST $CLI volume set $V0 cluster.heal-timeout 10
TEST $CLI volume start $V0
sleep 5

# Mount the volume
TEST glusterfs --volfile-id=/$V0 --volfile-server=$H0 $M0 \
  --attribute-timeout=0 --entry-timeout=0

# Kill bricks 1
TEST kill_brick $V0 $H0 $B0/${V0}1
sleep 1

# Write some data into the mount which will require healing
cd $M0
for i in {1..1000}; do
  dd if=/dev/urandom of=testdata_$i bs=64k count=1 2>/dev/null
done

# Re-start the brick
TEST $CLI volume start $V0 force
EXPECT_WITHIN 20 "1" afr_child_up_status $V0 0

sleep 1
TEST hang_brick $V0 $H0 $B0/${V0}1
sleep 4
TEST wait_for_shd_no_sink 20
cleanup

#G_TESTDEF_TEST_STATUS_CENTOS6=BAD_TEST,BUG=000000
#G_TESTDEF_TEST_STATUS_NETBSD7=BAD_TEST,BUG=000000
