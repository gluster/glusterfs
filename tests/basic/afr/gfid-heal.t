#!/bin/bash
#gfid self-heal test on distributed replica. Make sure all the gfids are same
#and the gfid exists on all the bricks

. $(dirname $0)/../../include.rc
. $(dirname $0)/../../volume.rc

cleanup;

function get_gfid_count {
        getfattr -d -m. -e hex $B0/brick{0,1,2,3,4,5}/$1 2>&1 | grep trusted.gfid | wc -l
}

TEST glusterd
TEST pidof glusterd
TEST $CLI volume create $V0 replica 2 $H0:$B0/brick{0,1,2,3,4,5}
TEST $CLI volume start $V0
TEST $GFS --volfile-id=$V0 --volfile-server=$H0 $M0;

TEST mkdir $B0/brick{0,1,2,3}/{0..9}
sleep 2 #to prevent is_fresh_file code path
TEST stat $M0/{0..9}
EXPECT 6 get_gfid_count 0
EXPECT 6 get_gfid_count 1
EXPECT 6 get_gfid_count 2
EXPECT 6 get_gfid_count 3
EXPECT 6 get_gfid_count 4
EXPECT 6 get_gfid_count 5
EXPECT 6 get_gfid_count 6
EXPECT 6 get_gfid_count 7
EXPECT 6 get_gfid_count 8
EXPECT 6 get_gfid_count 9
cleanup;
