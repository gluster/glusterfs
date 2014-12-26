#!/bin/bash

. $(dirname $0)/../../include.rc
. $(dirname $0)/../../volume.rc

#This tests if pathinfo getxattr fails when one of the bricks is down
#Lets hope it doesn't

cleanup;
function get_pathinfo_in_loop {
        failed=0
        for i in {1..1000}
        do
                getfattr -n trusted.glusterfs.pathinfo $M0 2>/dev/null
                if [ $? -ne 0 ]; then failed=1;break; fi
        done
        return $failed
}

TEST glusterd
TEST pidof glusterd
TEST $CLI volume create $V0 replica 2 $H0:$B0/${V0}{0,1}
TEST $CLI volume start $V0
TEST glusterfs --volfile-id=/$V0 --volfile-server=$H0 $M0 --attribute-timeout=0 --entry-timeout=0
cd $M0
TEST kill_brick $V0 $H0 $B0/${V0}1

#when one of the bricks is down getfattr of pathinfo should not fail
#Lets just do the test for 1000 times to see if we hit the race
TEST get_pathinfo_in_loop

cleanup
