#!/bin/bash

. $(dirname $0)/../include.rc
. $(dirname $0)/../volume.rc
. $(dirname $0)/../nfs.rc

function file_exists
{
        if [ -f $1 ]; then echo "Y"; else echo "N"; fi
}

cleanup;

TEST glusterd
TEST pidof glusterd
TEST $CLI volume create $V0 replica 3 $H0:$B0/${V0}{0,1,2}
TEST $CLI volume set $V0 config.gfproxyd enable
TEST $CLI volume set $V0 failover-hosts "127.0.0.1,192.168.122.215,192.168.122.90"
TEST $CLI volume set $V0 client-log-level TRACE
TEST $CLI volume start $V0

sleep 2

REGULAR_CLIENT_VOLFILE="/var/lib/glusterd/vols/${V0}/trusted-${V0}.tcp-fuse.vol"
GFPROXY_CLIENT_VOLFILE="/var/lib/glusterd/vols/${V0}/trusted-${V0}.tcp-gfproxy-fuse.vol"
GFPROXYD_VOLFILE="/var/lib/glusterd/vols/${V0}/${V0}.gfproxyd.vol"

# Client volfile must exist
TEST [ -f $GFPROXY_CLIENT_VOLFILE ]

# write-behind translators must exist
TEST grep "performance/write-behind" $GFPROXY_CLIENT_VOLFILE

# Make sure we didn't screw up the existing client
TEST grep "performance/write-behind" $REGULAR_CLIENT_VOLFILE
TEST grep "cluster/replicate" $REGULAR_CLIENT_VOLFILE
TEST grep "cluster/distribute" $REGULAR_CLIENT_VOLFILE

TEST [ -f $GFPROXYD_VOLFILE ]

TEST grep "cluster/replicate" $GFPROXYD_VOLFILE
TEST grep "cluster/distribute" $GFPROXYD_VOLFILE

# write-behind must *not* exist
TEST ! grep "performance/write-behind" $GFPROXYD_VOLFILE

# Test that we can start the server and the client
TEST glusterfs --thin-client --volfile-id=patchy --volfile-server=$H0 -l /var/log/glusterfs/${V0}-gfproxy-client.log $M0
sleep 2
TEST grep gfproxy-client/${V0} /proc/mounts

# Write data to the mount and checksum it
TEST dd if=/dev/urandom bs=1M count=10 of=/tmp/testfile1
md5=$(md5sum /tmp/testfile1 | awk '{print $1}')
TEST cp -v /tmp/testfile1 $M0/testfile1
TEST [ "$(md5sum $M0/testfile1 | awk '{print $1}')" == "$md5" ]

rm /tmp/testfile1

dd if=/dev/zero of=$M0/bigfile bs=1K count=10240 &
BG_STRESS_PID=$!

TEST wait $BG_STRESS_PID

# Perform graph change and make sure the gfproxyd restarts
TEST $CLI volume set $V0 stat-prefetch off

EXPECT_WITHIN $PROCESS_UP_TIMEOUT "Y" file_exists $M0/bigfile

cleanup;
#G_TESTDEF_TEST_STATUS_NETBSD7=1501392
