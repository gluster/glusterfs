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
TEST glusterfs --volfile-id=$V0 --volfile-server=$H0 $M0 --entry-timeout=0 --attribute-timeout=0;

#Test
TEST touch $M0/file
TEST setfattr -n trusted.gfid -v 0sBfz5vAdHTEK1GZ99qjqTIg== $B0/brick0/file
TEST ! "find $M0/file | xargs stat"

#Cleanup
EXPECT_WITHIN $UMOUNT_TIMEOUT "Y" force_umount $M0
TEST $CLI volume stop $V0
TEST $CLI volume delete $V0
TEST rm -rf $B0/*
