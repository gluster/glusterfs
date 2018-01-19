#!/bin/bash

#Create gfid split-brain of directory and check if conservative merge
#completes successfully.

. $(dirname $0)/../../include.rc
. $(dirname $0)/../../volume.rc
. $(dirname $0)/../../afr.rc

function check_sh_entries() {
        local expected="$1"
        local count=
        local good="0"
        shift

        for i in $*; do
                count="$(count_sh_entries $i)"
                if [[ "x${count}" == "x${expected}" ]]; then
                        good="$((good + 1))"
                fi
        done
        if [[ "x${good}" != "x${last_good}" ]]; then
                last_good="${good}"
# This triggers a sweep of the heal index. However if more than one brick
# tries to heal the same directory at the same time, one of them will take
# the lock and the other will give up, waiting for the next heal cycle, which
# is set to 60 seconds (the minimum valid value). So, each time we detect
# that one brick has completed the heal, we trigger another heal.
                $CLI volume heal $V0
        fi

        echo "${good}"
}

cleanup;

TEST glusterd
TEST pidof glusterd

TEST $CLI volume create $V0 replica 2 $H0:$B0/brick{0,1}
TEST $CLI volume set $V0 cluster.heal-timeout 60
TEST $CLI volume set $V0 cluster.self-heal-daemon off
TEST $CLI volume set $V0 stat-prefetch off
TEST $CLI volume start $V0
TEST $GFS --volfile-id=$V0 --volfile-server=$H0 $M0;

#Create files with alternate brick down. One file has gfid mismatch.
TEST mkdir $M0/DIR

TEST kill_brick $V0 $H0 $B0/brick1
TEST touch $M0/DIR/FILE
TEST touch $M0/DIR/file{1..5}
TEST $CLI volume start $V0 force
EXPECT_WITHIN $CHILD_UP_TIMEOUT "1" afr_child_up_status $V0 1

TEST kill_brick $V0 $H0 $B0/brick0
TEST touch $M0/DIR/FILE
TEST touch $M0/DIR/file{6..10}
TEST $CLI volume start $V0 force
EXPECT_WITHIN $CHILD_UP_TIMEOUT "1" afr_child_up_status $V0 0

#Trigger heal and verify number of entries in backend
TEST $CLI volume set $V0 cluster.self-heal-daemon on
EXPECT_WITHIN $PROCESS_UP_TIMEOUT "Y" glustershd_up_status
EXPECT_WITHIN $CHILD_UP_TIMEOUT "1" afr_child_up_status_in_shd $V0 0
EXPECT_WITHIN $CHILD_UP_TIMEOUT "1" afr_child_up_status_in_shd $V0 1

TEST $CLI volume heal $V0

last_good=""

EXPECT_WITHIN $HEAL_TIMEOUT "2" check_sh_entries 2 $B0/brick{0,1}

#Two entries for DIR and two for FILE
EXPECT_WITHIN $HEAL_TIMEOUT "4" get_pending_heal_count $V0
TEST diff <(ls $B0/brick0/DIR) <(ls $B0/brick1/DIR)
cleanup
