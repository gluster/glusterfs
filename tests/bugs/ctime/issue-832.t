#!/bin/bash

. $(dirname $0)/../../include.rc
. $(dirname $0)/../../volume.rc
. $(dirname $0)/../../traps.rc

#Trigger trusted.glusterfs.mdata setting codepath and see things work as expected
cleanup

TEST_USER=test-ctime-user
TEST_UID=27341

TEST useradd -o -M -u ${TEST_UID} ${TEST_USER}
push_trapfunc "userdel --force ${TEST_USER}"

TEST glusterd
TEST pidof glusterd

TEST $CLI volume create $V0 $H0:$B0/$V0
TEST $CLI volume start $V0

$GFS --volfile-id=/$V0 --volfile-server=$H0 $M0;
echo abc > $M0/test
TEST chmod 755 $M0/
TEST chmod 744 $M0/test
TEST setfattr -x trusted.glusterfs.mdata $B0/$V0/test
EXPECT_WITHIN $UMOUNT_TIMEOUT "Y" force_umount $M0
$GFS --volfile-id=/$V0 --volfile-server=$H0 $M0;
su ${TEST_USER} -c "cat $M0/test"
TEST getfattr -n trusted.glusterfs.mdata $B0/$V0/test

cleanup
