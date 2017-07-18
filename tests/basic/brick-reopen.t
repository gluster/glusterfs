#!/bin/bash

. $(dirname $0)/../include.rc
. $(dirname $0)/../volume.rc

function write() {
        dd if=/dev/urandom of=$M0/test bs=1k count=200
}

function swap_bricks {
        # swap brick 0 and 1 directories
        mv $B0/brick0 "$B0/brick0-tmp" &&
        mv $B0/brick1 $B0/brick0 &&
        mv "$B0/brick0-tmp" $B0/brick1
}

function kick_glusterd {
        killall glusterd &&
        ! pidof glusterd &&
        glusterd &&
        pidof glusterd

        if [ $? -eq 0 ]; then
                echo "Y"
        else
                echo "N"
        fi
}

function mute {
        cmd=$@
        $cmd &> /dev/null
        if [ $? -eq 0 ]; then
                echo "Y"
        else
                echo "N"
        fi
}

cleanup;

# Setup
TEST mute glusterd
TEST mute pidof glusterd
TEST mkdir -p $B0/brick{0,1}
TEST $CLI volume create $V0 replica 2 $H0:$B0/brick{0,1}

TEST $CLI volume start $V0
TEST glusterfs --volfile-id=$V0 --volfile-server=$H0 $M0 --entry-timeout=0 --attribute-timeout=0;
EXPECT_WITHIN $CHILD_UP_TIMEOUT "1" afr_child_up_status $V0 0

# Stop volume and force start volume
TEST write
TEST $CLI volume stop $V0
TEST ! write
TEST $CLI volume start $V0 force
EXPECT_WITHIN $CHILD_UP_TIMEOUT "1" afr_child_up_status $V0 0
TEST write

# Kill one brick and force restart volume
TEST kill_brick $V0 $H0 $B0/brick0
TEST $CLI volume start $V0 force
EXPECT_WITHIN $CHILD_UP_TIMEOUT "1" afr_child_up_status $V0 0
TEST write

# Kill one brick and kill mgmt
TEST kill_brick $V0 $H0 $B0/brick0
TEST kick_glusterd
EXPECT_WITHIN $CHILD_UP_TIMEOUT "1" afr_child_up_status $V0 0
TEST write

# Stop volume, swap brick directories and force start volume
TEST $CLI volume stop $V0
TEST swap_bricks
TEST ! $CLI volume start $V0 force
EXPECT "0" afr_child_up_status $V0 0
TEST ! write

# Re-swap the brick directories and force start volume
TEST swap_bricks
TEST $CLI volume start $V0 force
EXPECT_WITHIN $CHILD_UP_TIMEOUT "1" afr_child_up_status $V0 0
TEST write

# retest scenario with kick glusterd
TEST kill_brick $V0 $H0 $B0/brick0
TEST kill_brick $V0 $H0 $B0/brick1
TEST swap_bricks
TEST kick_glusterd
EXPECT_WITHIN $CHILD_UP_TIMEOUT "0" afr_child_up_status $V0 0
TEST ! write

TEST swap_bricks
TEST kick_glusterd
EXPECT_WITHIN $CHILD_UP_TIMEOUT "1" afr_child_up_status $V0 0
TEST write

# test backward compatibility
TEST $CLI volume stop $V0
TEST mute setfattr -x "trusted.glusterfs.brick-path" $B0/${V0}0
TEST mute ! getfattr -n "trusted.glusterfs.brick-path" $B0/${V0}0
TEST $CLI volume start $V0 force
EXPECT_WITHIN $CHILD_UP_TIMEOUT "1" afr_child_up_status $V0 0
TEST mute getfattr -n "trusted.glusterfs.brick-path" $B0/${V0}0
TEST $CLI volume stop $V0
TEST $CLI volume start $V0 force
EXPECT_WITHIN $CHILD_UP_TIMEOUT "1" afr_child_up_status $V0 0

# redo the test with kicking glusterd
TEST kill_brick $V0 $H0 $B0/brick0
TEST mute setfattr -x "trusted.glusterfs.brick-path" $B0/${V0}0
TEST mute ! getfattr -n "trusted.glusterfs.brick-path" $B0/${V0}0
TEST kick_glusterd
EXPECT_WITHIN $CHILD_UP_TIMEOUT "1" afr_child_up_status $V0 0
TEST mute getfattr -n "trusted.glusterfs.brick-path" $B0/${V0}0
TEST kill_brick $V0 $H0 $B0/brick0
TEST kick_glusterd
EXPECT_WITHIN $CHILD_UP_TIMEOUT "1" afr_child_up_status $V0 0

# replace brick and force start volume
TEST kill_brick $V0 $H0 $B0/brick0
TEST gluster volume replace-brick $V0 $H0:$B0/brick0 $H0:$B0/brick3 commit force
TEST $CLI volume start $V0 force
EXPECT_WITHIN $CHILD_UP_TIMEOUT "1" afr_child_up_status $V0 0
TEST write

cleanup

