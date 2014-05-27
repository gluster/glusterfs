#!/bin/bash
. $(dirname $0)/../include.rc
. $(dirname $0)/../volume.rc
. $(dirname $0)/../afr.rc
cleanup;

TEST glusterd
TEST pidof glusterd
TEST $CLI volume create $V0 replica 2 $H0:$B0/${V0}{0,1}
TEST $CLI volume start $V0;

TEST glusterfs --volfile-server=$H0 --volfile-id=$V0 $M0;
kill_brick $V0 $H0 $B0/${V0}0
TEST touch $M0/f1
TEST setfattr -n "user.foo" -v "test" $M0/f1

BRICK=$B0"/${V0}1"

TEST $CLI volume start $V0 force
sleep 5
TEST $CLI volume heal $V0

# Wait for self-heal to complete
EXPECT_WITHIN 30 '1' count_sh_entries $BRICK;

TEST getfattr -n "user.foo" $B0/${V0}0/f1;

TEST $CLI volume stop $V0;
TEST $CLI volume delete $V0;

cleanup;
