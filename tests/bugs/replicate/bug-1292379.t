#!/bin/bash

. $(dirname $0)/../../include.rc
. $(dirname $0)/../../volume.rc
. $(dirname $0)/../../afr.rc
. $(dirname $0)/../../fileio.rc

cleanup

TEST glusterd
TEST pidof glusterd
TEST $CLI volume info

TEST $CLI volume create $V0 replica 2 $H0:$B0/${V0}{0,1}
TEST $CLI volume start $V0
EXPECT 'Started' volinfo_field $V0 'Status'
TEST $CLI volume set $V0 cluster.self-heal-daemon off
TEST $CLI volume set $V0 cluster.entry-self-heal off
TEST $CLI volume set $V0 cluster.data-self-heal off
TEST $CLI volume set $V0 cluster.metadata-self-heal off
TEST $CLI volume set $V0 cluster.eager-lock off

TEST glusterfs --volfile-id=$V0 --volfile-server=$H0 $M0

TEST wfd=`fd_available`
TEST fd_open $wfd "w" $M0/a

TEST fd_write $wfd "abcd"

# Kill brick-0
TEST kill_brick $V0 $H0 $B0/${V0}0

# While brick-0 is down, rename 'a' to 'b'
TEST mv $M0/a $M0/b

TEST fd_write $wfd "lmn"

TEST $CLI volume start $V0 force
EXPECT_WITHIN $CHILD_UP_TIMEOUT "1" afr_child_up_status $V0 0
TEST fd_write $wfd "pqrs"
TEST $CLI volume set $V0 self-heal-daemon on
EXPECT_WITHIN $CHILD_UP_TIMEOUT "1" afr_child_up_status_in_shd $V0 0
EXPECT_WITHIN $CHILD_UP_TIMEOUT "1" afr_child_up_status_in_shd $V0 1

TEST $CLI volume heal $V0

EXPECT_WITHIN $HEAL_TIMEOUT "^0$" get_pending_heal_count $V0

TEST fd_write $wfd "xyz"
TEST fd_close $wfd

md5sum_b0=$(md5sum $B0/${V0}0/b | awk '{print $1}')

EXPECT "$md5sum_b0" echo `md5sum $B0/${V0}1/b | awk '{print $1}'`

TEST umount $M0
cleanup
