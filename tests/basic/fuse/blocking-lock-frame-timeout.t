#!/bin/bash

. $(dirname $0)/../../include.rc
. $(dirname $0)/../../volume.rc

lock_helper=$(dirname $0)/blocking-lock-frame-timeout
holder_pid=""
waiter_pid=""

function marker_exists {
    if [ -e "$1" ]; then
        echo Y
    else
        echo N
    fi
}

function cleanup_test {
    [ -n "$waiter_pid" ] && kill "$waiter_pid" >/dev/null 2>&1 || true
    [ -n "$holder_pid" ] && kill "$holder_pid" >/dev/null 2>&1 || true
    cleanup_tester "$lock_helper"
    cleanup
}

cleanup
trap cleanup_test EXIT

TEST build_tester ${lock_helper}.c
TEST glusterd
TEST pidof glusterd
TEST $CLI volume create $V0 $H0:$B0/${V0}1
TEST $CLI volume set $V0 network.frame-timeout 1
TEST $CLI volume start $V0
TEST $GFS --volfile-id=/$V0 --volfile-server=$H0 $M0

holder_ready=$B0/$V0-holder-ready
holder_release=$B0/$V0-holder-release
waiter_acquired=$B0/$V0-waiter-acquired

TEST mkfifo $holder_release
$lock_helper $M0/lockfile $holder_ready $holder_release &
holder_pid=$!
EXPECT_WITHIN $PROCESS_UP_TIMEOUT Y marker_exists $holder_ready

$lock_helper $M0/lockfile $waiter_acquired &
waiter_pid=$!

# Lock RPCs can legitimately wait without a deadline.  The RPC bailout timer
# runs every ten seconds, so keep the lock blocked long enough to cross both
# the configured frame timeout and a bailout scan.
sleep 12
TEST kill -0 $waiter_pid
EXPECT N marker_exists $waiter_acquired

TEST dd if=/dev/zero of=$holder_release bs=1 count=1 status=none
EXPECT_WITHIN $PROCESS_UP_TIMEOUT Y marker_exists $waiter_acquired
TEST wait $waiter_pid
waiter_pid=""
TEST wait $holder_pid
holder_pid=""

trap - EXIT
cleanup_test
