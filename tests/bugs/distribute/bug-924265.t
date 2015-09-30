#!/bin/bash

# Test that setting cluster.dht-xattr-name works, and that DHT consistently
# uses the specified name instead of the default.

. $(dirname $0)/../../include.rc
. $(dirname $0)/../../volume.rc

# We only care about the exit code, so keep it quiet.
function silent_getfattr {
    getfattr $* &> /dev/null
}

cleanup

TEST glusterd
TEST pidof glusterd

mkdir -p $H0:$B0/${V0}0

# Create a volume and set the option.
TEST $CLI volume create $V0 $H0:$B0/${V0}0
TEST $CLI volume set $V0 cluster.dht-xattr-name trusted.foo.bar

# Start and mount the volume.
TEST $CLI volume start $V0
EXPECT_WITHIN $PROCESS_UP_TIMEOUT 'Started' volinfo_field $V0 'Status';
TEST glusterfs --volfile-server=$H0 --volfile-id=$V0 $M0

# Create a directory and make sure it has the right xattr.
mkdir $M0/test
TEST ! silent_getfattr -n trusted.glusterfs.dht $B0/${V0}0/test
TEST silent_getfattr -n trusted.foo.bar $B0/${V0}0/test

cleanup
