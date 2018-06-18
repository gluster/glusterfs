#!/bin/bash
. $(dirname $0)/../../include.rc
. $(dirname $0)/../../volume.rc
. $(dirname $0)/../../afr.rc
cleanup;

###############################################################################
TEST glusterd
TEST pidof glusterd
TEST $CLI volume create $V0 replica 3 $H0:$B0/${V0}{0,1,2}
TEST $CLI volume set $V0 performance.stat-prefetch off
TEST $CLI volume set $V0 ctime on
TEST $CLI volume set $V0 utime on
TEST $CLI volume start $V0

TEST glusterfs --volfile-id=$V0 --volfile-server=$H0 --entry-timeout=0 $M0;

cd $M0
TEST "echo hello_world > FILE"
TEST ln -s FILE SOFTLINK

# Remove symlink only (not the .glusterfs entry) and trigger named heal.
TEST rm -f $B0/${V0}2/SOFTLINK
TEST stat SOFTLINK

# To heal and clear new-entry mark on source bricks.
TEST $CLI volume heal $V0
EXPECT_WITHIN $HEAL_TIMEOUT "^0$" get_pending_heal_count $V0

EXPECT 2 stat -c %h  $B0/${V0}2/SOFTLINK
EXPECT "hello_world" cat $B0/${V0}2/SOFTLINK

cd -
cleanup
###############################################################################

TEST glusterd
TEST pidof glusterd
TEST $CLI volume create $V0 disperse 3 redundancy 1  $H0:$B0/${V0}{0,1,2}
TEST $CLI volume set $V0 performance.stat-prefetch off
TEST $CLI volume set $V0 ctime on
TEST $CLI volume set $V0 utime on
TEST $CLI volume start $V0

TEST glusterfs --volfile-id=$V0 --volfile-server=$H0 --entry-timeout=0 $M0;

cd $M0
TEST "echo hello_world > FILE"
TEST ln -s FILE SOFTLINK

# Remove symlink only (not the .glusterfs entry) and trigger named heal.
TEST rm -f $B0/${V0}2/SOFTLINK
TEST stat SOFTLINK

# To heal and clear new-entry mark on source bricks.
TEST $CLI volume heal $V0
EXPECT_WITHIN $HEAL_TIMEOUT "^0$" get_pending_heal_count $V0

EXPECT 2 stat -c %h  $B0/${V0}2/SOFTLINK
TEST kill_brick $V0 $H0 $B0/${V0}0
cd -
EXPECT_WITHIN $UMOUNT_TIMEOUT "Y" force_umount $M0
TEST glusterfs --volfile-id=$V0 --volfile-server=$H0 --entry-timeout=0 $M0;
cd $M0
EXPECT "hello_world" cat SOFTLINK

cd -
cleanup
###############################################################################
