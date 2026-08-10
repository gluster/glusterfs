#!/bin/bash

. $(dirname $0)/../../include.rc
. $(dirname $0)/../../volume.rc

io_pid=""

function process_state {
    if kill -0 "$1" >/dev/null 2>&1; then
        echo Y
    else
        echo N
    fi
}

function log_count {
    grep -E -c "$1" "$2" 2>/dev/null || true
}

function log_count_increased {
    if [ "$(log_count "$2" "$3")" -gt "$1" ]; then
        echo Y
    else
        echo N
    fi
}

function cleanup_test {
    [ -n "$io_pid" ] && kill "$io_pid" >/dev/null 2>&1 || true
    cleanup
}

cleanup
trap cleanup_test EXIT

TEST glusterd
TEST pidof glusterd
TEST $CLI volume create $V0 $H0:$B0/${V0}1
TEST $CLI volume set $V0 network.frame-timeout 1
TEST $CLI volume set $V0 delay-gen posix
TEST $CLI volume set $V0 delay-gen.delay-duration 30000000
TEST $CLI volume set $V0 delay-gen.delay-percentage 100
TEST $CLI volume set $V0 delay-gen.enable fsync
TEST $CLI volume start $V0

client_log=$B0/$V0-client.log
TEST $GFS --volfile-id=/$V0 --volfile-server=$H0 \
          --log-file=$client_log --log-level=DEBUG $M0

mount_pid=$(get_mount_process_pid $V0)
connected_pattern="Connected, attached to remote volume"
disconnected_pattern="disconnected from client"
late_reply_pattern="reply for unknown or expired xid|cannot lookup the saved frame corresponding to xid"
connected_before=$(log_count "$connected_pattern" "$client_log")
disconnected_before=$(log_count "$disconnected_pattern" "$client_log")
late_replies_before=$(log_count "$late_reply_pattern" "$client_log")

# FSYNC is an ordinary RPC.  Let its frame time out while delay-gen keeps the
# valid reply in flight long enough for it to arrive after the frame is gone.
dd if=/dev/zero of=$M0/delayed-fsync bs=1 count=1 conv=fsync >/dev/null 2>&1 &
io_pid=$!
EXPECT_WITHIN $PROCESS_UP_TIMEOUT N process_state $io_pid
kill "$io_pid" >/dev/null 2>&1 || true
wait $io_pid >/dev/null 2>&1 || true
io_pid=""

EXPECT_WITHIN 40 Y log_count_increased "$late_replies_before" \
              "$late_reply_pattern" "$client_log"
EXPECT "$connected_before" log_count "$connected_pattern" "$client_log"
EXPECT "$disconnected_before" log_count "$disconnected_pattern" "$client_log"
EXPECT $mount_pid get_mount_process_pid $V0
TEST touch $M0/unrelated-io
TEST stat $M0/unrelated-io

trap - EXIT
cleanup_test
