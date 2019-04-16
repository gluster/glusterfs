#!/bin/bash

. $(dirname $0)/../../include.rc
. $(dirname $0)/../../volume.rc
. $(dirname $0)/../../fileio.rc

cleanup

TEST glusterd
TEST pidof glusterd
TEST $CLI volume create $V0 disperse 6 redundancy 2 $H0:$B0/${V0}{0..5}
TEST $CLI volume heal $V0 disable
TEST $CLI volume set $V0 disperse.background-heals 0
TEST $CLI volume set $V0 write-behind off
TEST $CLI volume set $V0 open-behind off
TEST $CLI volume start $V0
TEST glusterfs --entry-timeout=0 --attribute-timeout=0 -s $H0 --volfile-id $V0 $M0
EXPECT_WITHIN $CHILD_UP_TIMEOUT "6" ec_child_up_count $V0 0

TEST mkdir -p $M0/dir

fd="$(fd_available)"

TEST kill_brick $V0 $H0 $B0/${V0}0
EXPECT_WITHIN $CHILD_UP_TIMEOUT "5" ec_child_up_count $V0 0

TEST fd_open ${fd} rw $M0/dir/test
TEST fd_write ${fd} "test1"
TEST $CLI volume replace-brick ${V0} $H0:$B0/${V0}0 $H0:$B0/${V0}0_1 commit force
EXPECT_WITHIN $CHILD_UP_TIMEOUT "6" ec_child_up_count $V0 0
TEST fd_write ${fd} "test2"
TEST fd_close ${fd}

cleanup
