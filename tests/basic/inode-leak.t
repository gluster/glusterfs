#!/bin/bash

. $(dirname $0)/../include.rc
. $(dirname $0)/../volume.rc

function get_mount_active_size_value {
        local vol=$1
        local statedump=$(generate_mount_statedump $vol)
        sleep 1
        local val=$(grep "active_size" $statedump | cut -f2 -d'=' | tail -1)
        rm -f $statedump
        echo $val
}

function get_mount_lru_size_value {
        local vol=$1
        local statedump=$(generate_mount_statedump $vol)
        sleep 1
        local val=$(grep "lru_size" $statedump | cut -f2 -d'=' | tail -1)
        rm -f $statedump
        echo $val
}
cleanup

TEST glusterd
TEST pidof glusterd
TEST $CLI volume create $V0 replica 2 $H0:$B0/${V0}{0,1,2,3}
TEST $CLI volume start $V0
TEST glusterfs -s $H0 --volfile-id $V0 $M0

EXPECT "1" get_mount_active_size_value $V0
EXPECT "0" get_mount_lru_size_value $V0

TEST cp -rf /etc $M0
TEST find $M0
TEST rm -rf $M0/*

EXPECT "1" get_mount_active_size_value $V0
EXPECT "0" get_mount_lru_size_value $V0

cleanup

# Mainly marking it as known-issue as it is taking a *lot* of time.
# Revert back if we are below an hour in regression runs.
# Or consider running only in nightly regressions.

#G_TESTDEF_TEST_STATUS_NETBSD7=KNOWN_ISSUE,BUG=000000
#G_TESTDEF_TEST_STATUS_CENTOS6=KNOWN_ISSUE,BUG=000000
