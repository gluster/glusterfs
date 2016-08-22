#!/bin/bash
. $(dirname $0)/../../include.rc
. $(dirname $0)/../../volume.rc
cleanup;

TEST glusterd
TEST pidof glusterd
TEST $CLI volume create $V0 replica 2 $H0:$B0/${V0}{0,1}
TEST $CLI volume start $V0

TEST glusterfs --volfile-id=$V0 --volfile-server=$H0 $M0;
# Create files
for i in {1..5}
do
        echo $i > $M0/file$i.txt
done

# Negative case with brick not killed && volume-id xattrs present
TEST ! $CLI volume reset-brick $V0 $H0:$B0/${V0}1 $H0:$B0/${V0}1 commit force
TEST kill_brick $V0 $H0 $B0/${V0}1

# Negative case with brick killed but volume-id xattr present
TEST ! $CLI volume reset-brick $V0 $H0:$B0/${V0}1 $H0:$B0/${V0}1 commit

TEST $CLI volume reset-brick $V0 $H0:$B0/${V0}1 start
# Simulated reset disk
for i in {1..5}
do
        rm -rf $B0/${V0}{1}/file$i.txt
done
for i in {6..10}
do
        echo $i > $M0/file$i.txt
done

# Now reset the brick
TEST $CLI volume reset-brick $V0 $H0:$B0/${V0}1 $H0:$B0/${V0}1 commit force

EXPECT_WITHIN $PROCESS_UP_TIMEOUT "1" afr_child_up_status $V0 0
EXPECT_WITHIN $PROCESS_UP_TIMEOUT "1" afr_child_up_status $V0 1

EXPECT_WITHIN $PROCESS_UP_TIMEOUT "Y" glustershd_up_status
EXPECT_WITHIN $CHILD_UP_TIMEOUT "1" afr_child_up_status_in_shd $V0 0
EXPECT_WITHIN $CHILD_UP_TIMEOUT "1" afr_child_up_status_in_shd $V0 1

TEST $CLI volume heal $V0

EXPECT_WITHIN $HEAL_TIMEOUT "0" get_pending_heal_count $V0

# Check if entry-heal has happened
TEST diff <(ls $B0/${V0}0 | sort) <(ls $B0/${V0}1 | sort)
EXPECT "10" echo $(ls $B0/${V0}1 | wc -l)

cleanup;
