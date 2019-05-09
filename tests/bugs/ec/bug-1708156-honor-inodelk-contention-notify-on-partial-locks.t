#!/bin/bash

. $(dirname $0)/../../include.rc
. $(dirname $0)/../../volume.rc

function do_ls() {
    local dir="${1}"
    local i

    for i in {1..50}; do
        ls -l $M0/${dir} >/dev/null &
        ls -l $M1/${dir} >/dev/null &
        ls -l $M2/${dir} >/dev/null &
        ls -l $M3/${dir} >/dev/null &
    done
    wait
}

function measure_time() {
    {
        LC_ALL=C
        time -p "${@}"
    } 2>&1 | awk '/^real/ { print $2 * 1000 }'
}

cleanup

TEST glusterd
TEST pidof glusterd
TEST $CLI volume create $V0 disperse 6 redundancy 2 $H0:$B0/${V0}{0..5}

TEST $CLI volume set $V0 disperse.eager-lock on
TEST $CLI volume set $V0 disperse.other-eager-lock on
TEST $CLI volume set $V0 features.locks-notify-contention on
TEST $CLI volume set $V0 disperse.eager-lock-timeout 10
TEST $CLI volume set $V0 disperse.other-eager-lock-timeout 10

TEST $CLI volume start $V0
TEST $GFS --volfile-id=/$V0 --volfile-server=$H0 $M0
TEST $GFS --volfile-id=/$V0 --volfile-server=$H0 $M1
TEST $GFS --volfile-id=/$V0 --volfile-server=$H0 $M2
TEST $GFS --volfile-id=/$V0 --volfile-server=$H0 $M3
EXPECT_WITHIN $CHILD_UP_TIMEOUT "6" ec_child_up_count $V0 0 $M0
EXPECT_WITHIN $CHILD_UP_TIMEOUT "6" ec_child_up_count $V0 0 $M1
EXPECT_WITHIN $CHILD_UP_TIMEOUT "6" ec_child_up_count $V0 0 $M2
EXPECT_WITHIN $CHILD_UP_TIMEOUT "6" ec_child_up_count $V0 0 $M3
TEST mkdir $M0/dir
TEST touch $M0/dir/file.{1..10}

# Run multiple 'ls' concurrently from multiple clients so that they collide and
# cause partial locks.
TEST [[ $(measure_time do_ls dir) -lt 10000 ]]

cleanup
