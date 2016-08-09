#!/bin/bash

. $(dirname $0)/../../include.rc
. $(dirname $0)/../../volume.rc

function check_size
{
    for i in {1..10}; do
        size1=`stat -c %s $B0/${V0}0/tmp$i`
        size2=`stat -c %s $B0/${V0}1/tmp$i`
        if [[ $size1 -eq 0 ]] || [[ $size2 -eq 0 ]] || [[ $size1 -ne $size2 ]]; then
            return 1
        fi
    done

    return 0
}

cleanup;
TEST glusterd
TEST pidof glusterd

TEST $CLI volume create $V0 $H0:$B0/${V0}0;

TEST $CLI volume start $V0;

TEST glusterfs -s $H0 --volfile-id $V0 $M0

for i in {1..10}
do
        echo abc > $M0/tmp$i
done


# Add Another brick
TEST $CLI volume add-brick $V0 replica 2 $H0:$B0/${V0}1

#Check if self heal daemon has come up
EXPECT_WITHIN $PROCESS_UP_TIMEOUT "Y" glustershd_up_status

#Check if self heal daemon is able to see all bricks
EXPECT_WITHIN $CHILD_UP_TIMEOUT "1" afr_child_up_status_in_shd $V0 0
EXPECT_WITHIN $CHILD_UP_TIMEOUT "1" afr_child_up_status_in_shd $V0 1

# Wait for heal to complete
EXPECT_WITHIN $HEAL_TIMEOUT "^0$" get_pending_heal_count $V0

# Check if entry-heal has happened
TEST diff <(ls $B0/${V0}0 | sort) <(ls $B0/${V0}1 | sort)

#Check size of files on bricks
TEST check_size

cleanup;
