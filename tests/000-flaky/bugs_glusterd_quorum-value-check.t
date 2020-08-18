#!/bin/bash

. $(dirname $0)/../include.rc
. $(dirname $0)/../volume.rc

#G_TESTDEF_TEST_STATUS_CENTOS6=NFS_TEST

function check_quorum_nfs() {
    local qnfs="$(less /var/lib/glusterd/nfs/nfs-server.vol | grep "quorum-count"| awk '{print $3}')"
    local qinfo="$($CLI volume info $V0| grep "cluster.quorum-count"| awk '{print $2}')"

    if [ $qnfs = $qinfo ]; then
        echo "Y"
    else
        echo "N"
    fi
}

cleanup;

TEST glusterd
TEST pidof glusterd
TEST $CLI volume create $V0 replica 3 $H0:$B0/${V0}{0,1,2}
TEST $CLI volume set $V0 nfs.disable off
TEST $CLI volume set $V0 performance.write-behind off
TEST $CLI volume set $V0 cluster.self-heal-daemon off
TEST $CLI volume set $V0 cluster.quorum-type fixed
TEST $CLI volume start $V0

TEST $CLI volume set $V0 cluster.quorum-count 1
EXPECT_WITHIN $CONFIG_UPDATE_TIMEOUT "Y" check_quorum_nfs
TEST $CLI volume set $V0 cluster.quorum-count 2
EXPECT_WITHIN $CONFIG_UPDATE_TIMEOUT "Y" check_quorum_nfs
TEST $CLI volume set $V0 cluster.quorum-count 3
EXPECT_WITHIN $CONFIG_UPDATE_TIMEOUT "Y" check_quorum_nfs

cleanup;
