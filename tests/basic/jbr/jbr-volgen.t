#!/bin/bash

. $(dirname $0)/../../include.rc
. $(dirname $0)/../../volume.rc

volfiles=${GLUSTERD_WORKDIR}/vols/${V0}/
check_brick_volfiles () {
        for vf in ${volfiles}${V0}.$(hostname).*.vol; do
                grep -qs experimental/jbr $vf || return
                # At least for now, nothing else would put a client translator
                # in a brick volfile.
                grep -qs protocol/client $vf || return
        done
        echo "OK"
}

TEST glusterd
TEST pidof glusterd

TEST $CLI volume create $V0 replica 2 $H0:$B0/${V0}{1,2}
TEST $CLI volume set $V0 cluster.jbr on

# Check that the client volfile got modified properly.
TEST grep -qs experimental/jbrc ${volfiles}${V0}.tcp-fuse.vol

# Check that the brick volfiles got modified as well.
EXPECT "OK" check_brick_volfiles

# Put things back and make sure the "undo" worked.
TEST $CLI volume set $V0 cluster.jbr off
TEST $CLI volume start $V0
TEST $GFS -s $H0 --volfile-id $V0 $M0
echo hello > $M0/probe
EXPECT hello cat ${B0}/${V0}1/probe
EXPECT hello cat ${B0}/${V0}2/probe

cleanup
#G_TESTDEF_TEST_STATUS_CENTOS6=KNOWN_ISSUE,BUG=1385758
#G_TESTDEF_TEST_STATUS_NETBSD7=KNOWN_ISSUE,BUG=1385758
