#!/bin/bash

. $(dirname $0)/../include.rc
. $(dirname $0)/../snapshot.rc

function stat_testfile() {
        stat $M0/snaps/snaps/testfile
        echo $?
}

cleanup;
TEST verify_lvm_version;
TEST glusterd;
TEST pidof glusterd;

TEST setup_lvm 1

TEST $CLI volume create $V0 $H0:$L1
TEST $CLI volume start $V0
TEST glusterfs -s $H0 --volfile-id=$V0 $M0

TEST touch $M0/testfile

TEST $CLI snapshot create snaps $V0
TEST $CLI snapshot activate snaps
TEST $CLI volume set $V0 features.uss enable
TEST $CLI volume set $V0 snapshot-directory snaps

EXPECT_WITHIN 20 "0" stat_testfile

umount -f $M0

#Clean up
TEST $CLI snapshot delete snaps
TEST $CLI volume stop $V0 force
TEST $CLI volume delete $V0

cleanup;

