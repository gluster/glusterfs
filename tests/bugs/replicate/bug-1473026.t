#!/bin/bash

. $(dirname $0)/../../include.rc
. $(dirname $0)/../../volume.rc

#G_TESTDEF_TEST_STATUS_CENTOS6=KNOWN_ISSUE,BUG=1473026
#G_TESTDEF_TEST_STATUS_NETBSD7=KNOWN_ISSUE,BUG=1473026

cleanup;
TEST glusterd
TEST pidof glusterd

TEST $CLI volume create $V0 replica 3 $H0:$B0/${V0}{0,1,2}
TEST $CLI volume start $V0;

#kill one brick (this has some issue)
TEST kill_brick $V0 $H0 $B0/${V0}1

#kill the brick to be replaced
TEST kill_brick $V0 $H0 $B0/${V0}0

# We know this command would fail because file system is read only now
TEST ! $CLI volume replace-brick $V0 $H0:$B0/${V0}0 $H0:$B0/${V0}4 commit force

TEST pkill glusterd

# Glusterd should start but the volume info and brick volfiles don't match
TEST glusterd
TEST pidof glusterd

cleanup;
