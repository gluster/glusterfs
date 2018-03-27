#!/bin/bash

. $(dirname $0)/../../include.rc
. $(dirname $0)/../../traps.rc
. $(dirname $0)/../../volume.rc

function count_brick_processes {
        pgrep glusterfsd | wc -l
}

function count_brick_pids {
        $CLI --xml volume status all | sed -n '/.*<pid>\([^<]*\).*/s//\1/p' \
                                     | grep -v "N/A" | sort | uniq | wc -l
}

cleanup;

#bug-1451248 - validate brick mux after glusterd reboot

TEST glusterd
TEST $CLI volume set all cluster.brick-multiplex on
push_trapfunc "$CLI volume set all cluster.brick-multiplex off"
push_trapfunc "cleanup"

TEST $CLI volume create $V0 $H0:$B0/${V0}{1..3}
TEST $CLI volume start $V0

EXPECT 1 count_brick_processes
EXPECT 1 count_brick_pids
EXPECT_WITHIN $PROCESS_UP_TIMEOUT 3 online_brick_count

pkill gluster
TEST glusterd

EXPECT_WITHIN $PROCESS_UP_TIMEOUT 1 count_brick_processes
EXPECT_WITHIN $PROCESS_UP_TIMEOUT 1 count_brick_pids
EXPECT_WITHIN $PROCESS_UP_TIMEOUT 3 online_brick_count

TEST $CLI volume create $V1 $H0:$B0/${V1}{1..3}
TEST $CLI volume start $V1

EXPECT_WITHIN $PROCESS_UP_TIMEOUT 1 count_brick_processes
EXPECT_WITHIN $PROCESS_UP_TIMEOUT 1 count_brick_pids
EXPECT_WITHIN $PROCESS_UP_TIMEOUT 6 online_brick_count

#bug-1560957 - brick status goes offline after remove-brick followed by add-brick

pkill glusterd
TEST glusterd
TEST $CLI volume remove-brick $V0 $H0:$B0/${V0}1 force
TEST $CLI volume add-brick $V0 $H0:$B0/${V0}1_new force

EXPECT_WITHIN $PROCESS_UP_TIMEOUT 1 count_brick_processes
EXPECT_WITHIN $PROCESS_UP_TIMEOUT 1 count_brick_pids
EXPECT_WITHIN $PROCESS_UP_TIMEOUT 6 online_brick_count

#bug-1446172 - reset brick with brick multiplexing enabled

TEST glusterfs --volfile-id=$V0 --volfile-server=$H0 $M0;
# Create files
for i in {1..5}
do
        echo $i > $M0/file$i.txt
done

TEST $CLI volume reset-brick $V0 $H0:$B0/${V0}1_new start

EXPECT_WITHIN $PROCESS_DOWN_TIMEOUT 5 online_brick_count
EXPECT 1 count_brick_processes

# Negative case with brick killed but volume-id xattr present
TEST ! $CLI volume reset-brick $V0 $H0:$B0/${V0}1 $H0:$B0/${V0}1 commit

# reset-brick commit force should work and should bring up the brick
TEST $CLI volume reset-brick $V0 $H0:$B0/${V0}1_new $H0:$B0/${V0}1_new commit force

EXPECT_WITHIN $PROCESS_UP_TIMEOUT 6 online_brick_count
EXPECT 1 count_brick_processes
TEST glusterfs --volfile-id=$V1 --volfile-server=$H0 $M1;
# Create files
for i in {1..5}
do
        echo $i > $M1/file$i.txt
done

TEST $CLI volume reset-brick $V1 $H0:$B0/${V1}1 start
EXPECT_WITHIN $PROCESS_DOWN_TIMEOUT 5 online_brick_count
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

EXPECT_WITHIN $PROCESS_UP_TIMEOUT 6 online_brick_count
EXPECT 1 count_brick_processes
cleanup
