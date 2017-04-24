#!/bin/bash

. $(dirname $0)/../../include.rc
. $(dirname $0)/../../traps.rc
. $(dirname $0)/../../volume.rc

cleanup;

function count_up_bricks {
        $CLI --xml volume status | grep '<status>1' | wc -l
}

function count_brick_processes {
	pgrep glusterfsd | wc -l
}

TEST glusterd

TEST $CLI volume set all cluster.brick-multiplex on
push_trapfunc "$CLI volume set all cluster.brick-multiplex off"
push_trapfunc "cleanup"

TEST $CLI volume create $V0 $H0:$B0/${V0}{0,1}
TEST $CLI volume create $V1 $H0:$B0/${V1}{0,1}

TEST $CLI volume start $V0
TEST $CLI volume start $V1

EXPECT_WITHIN $PROCESS_UP_TIMEOUT 4 count_up_bricks
EXPECT 1 count_brick_processes

TEST glusterfs --volfile-id=$V0 --volfile-server=$H0 $M0;
# Create files
for i in {1..5}
do
        echo $i > $M0/file$i.txt
done

TEST $CLI volume reset-brick $V0 $H0:$B0/${V0}1 start

EXPECT_WITHIN $PROCESS_DOWN_TIMEOUT 3 count_up_bricks
EXPECT 1 count_brick_processes

# Negative case with brick killed but volume-id xattr present
TEST ! $CLI volume reset-brick $V0 $H0:$B0/${V0}1 $H0:$B0/${V0}1 commit

# reset-brick commit force should work and should bring up the brick
TEST $CLI volume reset-brick $V0 $H0:$B0/${V0}1 $H0:$B0/${V0}1 commit force

EXPECT_WITHIN $PROCESS_UP_TIMEOUT 4 count_up_bricks
EXPECT 1 count_brick_processes

TEST glusterfs --volfile-id=$V1 --volfile-server=$H0 $M1;
# Create files
for i in {1..5}
do
        echo $i > $M1/file$i.txt
done

TEST $CLI volume reset-brick $V1 $H0:$B0/${V1}1 start

EXPECT_WITHIN $PROCESS_DOWN_TIMEOUT 3 count_up_bricks
EXPECT 1 count_brick_processes

# Simulate reset disk
for i in {1..5}
do
        rm -rf $B0/${V1}1/file$i.txt
done

setfattr -x trusted.glusterfs.volume-id $B0/${V1}1
setfattr -x trusted.gfid $B0/${V1}1

# Test reset-brick commit. Using CLI_IGNORE_PARTITION since normal CLI  uses
# the --wignore flag that essentially makes the command act like "commit force"
TEST $CLI_IGNORE_PARTITION volume reset-brick $V1 $H0:$B0/${V1}1 $H0:$B0/${V1}1 commit

EXPECT_WITHIN $PROCESS_UP_TIMEOUT 4 count_up_bricks
EXPECT 1 count_brick_processes
