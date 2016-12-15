#!/bin/bash

. $(dirname $0)/../include.rc
. $(dirname $0)/../volume.rc
. $(dirname $0)/../nfs.rc

cleanup;

function start_gfproxyd {
        glusterfs --volfile-id=gfproxy/${V0} --volfile-server=$H0  -l /var/log/glusterfs/${V0}-gfproxy.log
}

function restart_gfproxyd {
        pkill -f gfproxy/${V0}
        start_gfproxyd
}

TEST glusterd
TEST pidof glusterd
TEST $CLI volume create $V0 replica 3 $H0:$B0/${V0}{0,1,2}
TEST $CLI volume set $V0 config.gfproxyd-remote-host $H0
TEST $CLI volume start $V0

sleep 2

REGULAR_CLIENT_VOLFILE="/var/lib/glusterd/vols/${V0}/trusted-${V0}.tcp-fuse.vol"
GFPROXY_CLIENT_VOLFILE="/var/lib/glusterd/vols/${V0}/trusted-${V0}.tcp-gfproxy-fuse.vol"
GFPROXYD_VOLFILE="/var/lib/glusterd/vols/${V0}/${V0}.gfproxyd.vol"

# Client volfile must exist
TEST [ -f $GFPROXY_CLIENT_VOLFILE ]

# AHA & write-behind translators must exist
TEST grep "cluster/aha"  $GFPROXY_CLIENT_VOLFILE
TEST grep "performance/write-behind" $GFPROXY_CLIENT_VOLFILE

# Make sure we didn't screw up the existing client
TEST grep "performance/write-behind" $REGULAR_CLIENT_VOLFILE
TEST grep "cluster/replicate" $REGULAR_CLIENT_VOLFILE
TEST grep "cluster/distribute" $REGULAR_CLIENT_VOLFILE

TEST [ -f $GFPROXYD_VOLFILE ]

TEST grep "cluster/replicate" $GFPROXYD_VOLFILE
TEST grep "cluster/distribute" $GFPROXYD_VOLFILE

# AHA & write-behind must *not* exist
TEST ! grep "cluster/aha"  $GFPROXYD_VOLFILE
TEST ! grep "performance/write-behind" $GFPROXYD_VOLFILE

# Test that we can start the server and the client
TEST start_gfproxyd
TEST glusterfs --volfile-id=gfproxy-client/${V0} --volfile-server=$H0 -l /var/log/glusterfs/${V0}-gfproxy-client.log $M0
sleep 2
TEST grep gfproxy-client/${V0} /proc/mounts

# Write data to the mount and checksum it
TEST dd if=/dev/urandom bs=1M count=10 of=/tmp/testfile1
md5=$(md5sum /tmp/testfile1 | awk '{print $1}')
TEST cp -v /tmp/testfile1 $M0/testfile1
TEST [ "$(md5sum $M0/testfile1 | awk '{print $1}')" == "$md5" ]

rm /tmp/testfile1

dd if=/dev/zero of=$N0/bigfile bs=1M count=10240 &
BG_STRESS_PID=$!

sleep 3

restart_gfproxyd

TEST wait $BG_STRESS_PID

cleanup;
