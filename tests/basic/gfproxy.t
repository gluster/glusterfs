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
TEST start_gfproxyd

cleanup;
