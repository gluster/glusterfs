#!/bin/bash
#gfid self-heal test on distributed replica. Make sure all the gfids are same
#and the gfid exists on all the bricks

. $(dirname $0)/../../include.rc
. $(dirname $0)/../../volume.rc
cleanup;

TEST glusterd
TEST pidof glusterd
TEST $CLI volume create $V0 replica 2 $H0:$B0/brick{0,1,2,3}
TEST $CLI volume start $V0
TEST $GFS --volfile-id=$V0 --volfile-server=$H0 $M0;

TEST mkdir $B0/brick{0,1,2,3}/d
sleep 2 #to prevent is_fresh_file code path
TEST stat $M0/d
gfid_count=$(getfattr -d -m. -e hex $B0/brick{0,1,2,3}/d 2>&1 | grep trusted.gfid | wc -l)
EXPECT 4 echo $gfid_count
cleanup;
