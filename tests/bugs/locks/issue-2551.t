#!/bin/bash

. $(dirname $0)/../../include.rc
. $(dirname $0)/../../volume.rc

function check_time() {
    local max="${1}"
    local start="$(date +"%s")"

    shift

    if "${@}"; then
        if [[ $(($(date +"%s") - ${start})) -lt ${max} ]]; then
            return 0
        fi
    fi

    return 1
}

cleanup

TEST glusterd
TEST pidof glusterd
TEST $CLI volume create $V0 disperse 3 redundancy 1 $H0:$B0/brick{0..2}
TEST $CLI volume set $V0 disperse.eager-lock on
TEST $CLI volume set $V0 disperse.eager-lock-timeout 30
TEST $CLI volume set $V0 features.locks-notify-contention on
TEST $CLI volume set $V0 performance.write-behind off
TEST $CLI volume set $V0 performance.open-behind off
TEST $CLI volume set $V0 performance.quick-read off

TEST $CLI volume start $V0
EXPECT_WITHIN $PROCESS_UP_TIMEOUT "1" brick_up_status $V0 $H0 $B0/brick0
EXPECT_WITHIN $PROCESS_UP_TIMEOUT "1" brick_up_status $V0 $H0 $B0/brick1
EXPECT_WITHIN $PROCESS_UP_TIMEOUT "1" brick_up_status $V0 $H0 $B0/brick2

TEST $GFS --volfile-id=$V0 --volfile-server=$H0 $M0
EXPECT_WITHIN $CHILD_UP_TIMEOUT "3" ec_child_up_count $V0 0 $M0

TEST mkdir $M0/dir
TEST dd if=/dev/zero of=$M0/dir/test bs=4k count=1
EXPECT_WITHIN $UMOUNT_TIMEOUT "Y" force_umount $M0

TEST $CLI volume stop $V0
TEST $CLI volume start $V0
EXPECT_WITHIN $PROCESS_UP_TIMEOUT "1" brick_up_status $V0 $H0 $B0/brick0
EXPECT_WITHIN $PROCESS_UP_TIMEOUT "1" brick_up_status $V0 $H0 $B0/brick1
EXPECT_WITHIN $PROCESS_UP_TIMEOUT "1" brick_up_status $V0 $H0 $B0/brick2

TEST $GFS --volfile-id=$V0 --volfile-server=$H0 $M0
EXPECT_WITHIN $CHILD_UP_TIMEOUT "3" ec_child_up_count $V0 0 $M0

TEST $GFS --volfile-id=$V0 --volfile-server=$H0 $M1
EXPECT_WITHIN $CHILD_UP_TIMEOUT "3" ec_child_up_count $V0 0 $M1

TEST dd if=/dev/zero of=$M0/dir/test bs=4k count=1 conv=notrunc
TEST check_time 5 dd if=/dev/zero of=$M1/dir/test bs=4k count=1 conv=notrunc
