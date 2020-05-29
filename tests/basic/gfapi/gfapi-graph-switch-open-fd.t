#!/bin/bash

. $(dirname $0)/../../include.rc
. $(dirname $0)/../../volume.rc

cleanup;

TEST glusterd

TEST $CLI volume create $V0 replica 3 ${H0}:$B0/brick{0..2};
EXPECT 'Created' volinfo_field $V0 'Status';

TEST $CLI volume start $V0;
EXPECT 'Started' volinfo_field $V0 'Status';

TEST $GFS --volfile-id=$V0 --volfile-server=$H0 $M0;
TEST touch $M0/sync
logdir=`gluster --print-logdir`

TEST build_tester $(dirname $0)/gfapi-keep-writing.c -lgfapi


#Launch a program to keep doing writes on an fd
./$(dirname $0)/gfapi-keep-writing ${H0} $V0 $logdir/gfapi-async-calls-test.log sync &
p=$!
sleep 1 #Let some writes go through
#Check if graph switch will lead to any pending markers for ever
TEST $CLI volume set $V0 performance.quick-read off
TEST $CLI volume set $V0 performance.io-cache off
TEST $CLI volume set $V0 performance.stat-prefetch off
TEST $CLI volume set $V0 performance.read-ahead off


TEST rm -f $M0/sync #Make sure the glfd is closed
TEST wait #Wait for background process to die
#Goal is to check if there is permanent FOOL changelog
sleep 5
EXPECT "0x000000000000000000000000" afr_get_changelog_xattr $B0/brick0/glfs_test.txt trusted.afr.dirty
EXPECT "0x000000000000000000000000" afr_get_changelog_xattr $B0/brick1/glfs_test.txt trusted.afr.dirty
EXPECT "0x000000000000000000000000" afr_get_changelog_xattr $B0/brick2/glfs_test.txt trusted.afr.dirty

cleanup_tester $(dirname $0)/gfapi-async-calls-test

cleanup;
