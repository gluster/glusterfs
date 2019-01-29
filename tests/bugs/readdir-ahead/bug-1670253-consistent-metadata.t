#!/bin/bash

. $(dirname $0)/../../include.rc
. $(dirname $0)/../../volume.rc

cleanup;

TEST glusterd

TEST $CLI volume create $V0 replica 3 $H0:$B0/${V0}{0,1,2}
TEST $CLI volume set $V0 readdir-ahead on #on by default as of writing this .t.
TEST $CLI volume set $V0 consistent-metadata on
TEST $CLI volume start $V0
TEST glusterfs --volfile-server=$H0 --volfile-id=$V0 $M0
TEST touch $M0/FILE
echo "abc" >> $M0/FILE
EXPECT "^0$" echo $?
EXPECT "abc" cat $M0/FILE
echo "truncate" >$M0/FILE
EXPECT "^0$" echo $?
EXPECT "truncate" cat $M0/FILE
EXPECT_WITHIN $UMOUNT_TIMEOUT "Y" force_umount $M0
cleanup;
