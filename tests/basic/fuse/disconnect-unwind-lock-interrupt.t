#!/bin/bash

. $(dirname $0)/../../include.rc
. $(dirname $0)/../../volume.rc

waiter_pid=""

function process_state {
    if kill -0 "$1" >/dev/null 2>&1; then
        echo Y
    else
        echo N
    fi
}

function mount_responds {
    if stat $M0/lockfile >/dev/null 2>&1; then
        echo Y
    else
        echo N
    fi
}

function cleanup_test {
    [ -n "$waiter_pid" ] && kill "$waiter_pid" >/dev/null 2>&1 || true
    exec 8>&-
    cleanup
}

cleanup
trap cleanup_test EXIT

TEST glusterd
TEST pidof glusterd
TEST $CLI volume create $V0 $H0:$B0/${V0}1
TEST $CLI volume set $V0 delay-gen locks
TEST $CLI volume set $V0 delay-gen.delay-duration 30000000
TEST $CLI volume set $V0 delay-gen.delay-percentage 100
TEST $CLI volume set $V0 delay-gen.enable fgetxattr
TEST $CLI volume start $V0
TEST $GFS --volfile-id=/$V0 --volfile-server=$H0 \
          --fuse-setlk-handle-interrupt=on $M0

mount_pid=$(get_mount_process_pid $V0)
TEST touch $M0/lockfile
exec 8>$M0/lockfile
TEST flock -x 8

# The timeout interrupts this blocked lock.  Its cancellation FGETXATTR is
# held in delay-gen so both it and the original LK are pending at disconnect.
flock -x -w 1 $M0/lockfile true &
waiter_pid=$!
sleep 2
EXPECT Y process_state $waiter_pid

TEST kill_brick $V0 $H0 $B0/${V0}1
EXPECT_WITHIN $PROCESS_UP_TIMEOUT N process_state $waiter_pid
kill "$waiter_pid" >/dev/null 2>&1 || true
wait $waiter_pid >/dev/null 2>&1 || true
waiter_pid=""
exec 8>&-

TEST $CLI volume start $V0 force
EXPECT_WITHIN $PROCESS_UP_TIMEOUT 1 brick_up_status $V0 $H0 $B0/${V0}1
EXPECT_WITHIN $CHILD_UP_TIMEOUT Y mount_responds
EXPECT $mount_pid get_mount_process_pid $V0

trap - EXIT
cleanup_test
