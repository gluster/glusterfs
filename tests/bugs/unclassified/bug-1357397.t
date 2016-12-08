#!/bin/bash

. $(dirname $0)/../../include.rc
. $(dirname $0)/../../volume.rc

cleanup

TEST glusterd
TEST pidof glusterd

TEST $CLI volume create $V0 replica 2 $H0:$B0/${V0}{1,2}

TEST $CLI volume start $V0

TEST $CLI volume set $V0 features.trash on
TEST $CLI volume set $V0 features.trash-internal-op on

TEST [ -e $B0/${V0}1/.trashcan ]

TEST [ -e $B0/${V0}1/.trashcan/internal_op ]

TEST $CLI volume stop $V0

rm -rf $B0/${V0}1/.trashcan/internal_op

TEST [ ! -e $B0/${V0}1/.trashcan/internal_op ]

TEST $CLI volume start $V0 force

TEST [ -e $B0/${V0}1/.trashcan/internal_op ]

cleanup

#G_TESTDEF_TEST_STATUS_CENTOS6=BAD_TEST,BUG=1385758
#G_TESTDEF_TEST_STATUS_NETBSD7=BAD_TEST,BUG=1385758
