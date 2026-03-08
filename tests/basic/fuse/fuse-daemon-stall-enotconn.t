#!/bin/bash

. $(dirname $0)/../../include.rc
. $(dirname $0)/../../volume.rc

holdfile=""

function cleanup_test {
        rm -f "$holdfile"
        force_umount "$M0" >/dev/null 2>&1 || true
        cleanup
}

function mount_log {
        local logdir=""

        logdir=$($CLI --print-logdir)
        ls "$logdir"/mnt-glusterfs-*.log 2>/dev/null | head -1
}

function hold_hook_logged {
        local logfile=""

        logfile=$(mount_log)
        grep -c "debug-disconnect-notify-holdfile is blocking RPC_CLNT_DISCONNECT notify" "$logfile"
}

function connect_count {
        local logfile=""

        logfile=$(mount_log)
        grep -c "Connected, attached to remote volume" "$logfile"
}

cleanup

holdfile="$B0/$V0-child-down.hold"
trap cleanup_test EXIT

TEST glusterd
TEST pidof glusterd

TEST $CLI volume create $V0 $H0:$B0/${V0}1
TEST $CLI volume set $V0 performance.quick-read off
TEST $CLI volume set $V0 performance.write-behind off
TEST $CLI volume set $V0 performance.io-cache off
TEST $CLI volume set $V0 performance.stat-prefetch off
TEST $CLI volume set $V0 performance.client-io-threads off
TEST $CLI volume set $V0 ping-timeout 2
TEST $CLI volume start $V0
EXPECT 'Started' volinfo_field $V0 'Status'

TEST touch "$holdfile"
TEST $GFS --xlator-option="$V0-client-0.debug-disconnect-notify-holdfile=$holdfile" \
        --volfile-id=/$V0 --volfile-server=$H0 $M0
TEST touch $M0/preflight

TEST kill_brick $V0 $H0 $B0/${V0}1
EXPECT_WITHIN $PROCESS_UP_TIMEOUT "0" brick_up_status $V0 $H0 $B0/${V0}1

# Wait until the client has entered the intentionally stalled disconnect notify.
EXPECT_WITHIN 8 "1" hold_hook_logged

TEST $CLI volume start $V0 force
EXPECT_WITHIN $PROCESS_UP_TIMEOUT "1" brick_up_status $V0 $H0 $B0/${V0}1

# This is the bug reproducer: reconnect should not be gated on synchronous
# disconnect notify. Current code schedules reconnect only after notify
# returns, so the second successful connect never appears while the holdfile
# exists.
EXPECT_WITHIN 8 "2" connect_count

trap - EXIT
cleanup_test
