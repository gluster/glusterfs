#!/bin/bash
#Test that GFID mismatches result in EIO

. $(dirname $0)/../../include.rc
. $(dirname $0)/../../volume.rc
cleanup;

#Init
TEST glusterd
TEST pidof glusterd
TEST $CLI volume create $V0 replica 2 $H0:$B0/brick{0,1}
TEST $CLI volume set $V0 self-heal-daemon off
TEST $CLI volume set $V0 stat-prefetch off
TEST $CLI volume start $V0
TEST $CLI volume set $V0 cluster.background-self-heal-count 0
# We can't count on brick0 getting a copy of the file immediately without this,
# because (especially with multiplexing) it might not have *come up*
# immediately.
TEST $CLI volume set $V0 cluster.quorum-type auto
TEST $GFS --volfile-id=$V0 -s $H0 $M0;

#Test
TEST touch $M0/file
TEST setfattr -n trusted.gfid -v 0sBfz5vAdHTEK1GZ99qjqTIg== $B0/brick0/file
TEST ! "find $M0/file"
TEST ! "stat $M0/file"

#Cleanup
EXPECT_WITHIN $UMOUNT_TIMEOUT "Y" force_umount $M0
TEST $CLI volume stop $V0
TEST $CLI volume delete $V0
TEST rm -rf $B0/*
