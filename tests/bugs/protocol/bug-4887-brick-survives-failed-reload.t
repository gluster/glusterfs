#!/bin/bash
#
# A brick that reloads a volfile whose topology differs from its running graph
# (any volume set that adds or removes a brick-side xlator) brings the new graph
# up in process. The new protocol/server cannot bind its port, which the live
# graph's listener holds, so server_init() fails as designed and the old graph
# keeps serving. Since 15837467fc (#4857) server_init()'s error path cleared the
# process-wide event pool in server_cleanup() and the brick dumped core on the
# next event (#4887). Every brick must survive such a reload as the same, live
# process and keep serving.

. $(dirname $0)/../../include.rc
. $(dirname $0)/../../volume.rc

# Failed-reload count in a brick log, minus $2 (the count already there before
# this test: cleanup does not clear the brick logs, only run-tests.sh does,
# between tests). Always exits 0: EXPECT_WITHIN stops retrying on a non-zero
# exit, and grep -c exits 1 while the count is still 0.
function listener_failed_count {
    local n
    n=$(grep -c 'creation of listener failed' "$1" 2>/dev/null)
    echo $(( ${n:-0} - ${2:-0} ))
}

function brick_log {
    echo "$LOGDIR/bricks/$(echo "$1" | sed -e 's|^/||' -e 's|/|-|g').log"
}

cleanup;

TEST glusterd
TEST pidof glusterd
TEST $CLI volume create $V0 replica 3 $H0:$B0/${V0}{0,1,2}
TEST $CLI volume start $V0
EXPECT 'Started' volinfo_field $V0 'Status'
EXPECT_WITHIN $PROCESS_UP_TIMEOUT "1" brick_up_status $V0 $H0 $B0/${V0}0
EXPECT_WITHIN $PROCESS_UP_TIMEOUT "1" brick_up_status $V0 $H0 $B0/${V0}1
EXPECT_WITHIN $PROCESS_UP_TIMEOUT "1" brick_up_status $V0 $H0 $B0/${V0}2
TEST $GFS --volfile-id=$V0 --volfile-server=$H0 $M0
TEST touch $M0/file

pid0=$(get_brick_pid $V0 $H0 $B0/${V0}0)
pid1=$(get_brick_pid $V0 $H0 $B0/${V0}1)
pid2=$(get_brick_pid $V0 $H0 $B0/${V0}2)
TEST [ -n "$pid0" -a -n "$pid1" -a -n "$pid2" ]

# A brick-side xlator is added: every brick reloads the new volfile in process,
# the new listener cannot bind and the init fails (expected, logged by each
# brick); the running graph must stay up.
base0=$(listener_failed_count $(brick_log $B0/${V0}0))
base1=$(listener_failed_count $(brick_log $B0/${V0}1))
base2=$(listener_failed_count $(brick_log $B0/${V0}2))
TEST $CLI volume set $V0 delay-gen locks
EXPECT_WITHIN $PROCESS_UP_TIMEOUT "^1$" listener_failed_count $(brick_log $B0/${V0}0) $base0
EXPECT_WITHIN $PROCESS_UP_TIMEOUT "^1$" listener_failed_count $(brick_log $B0/${V0}1) $base1
EXPECT_WITHIN $PROCESS_UP_TIMEOUT "^1$" listener_failed_count $(brick_log $B0/${V0}2) $base2

# The first event on each brick's sockets after the failed reload: with the bug
# this is where every brick crashed. The old graph must still serve it, and the
# bricks must be the same, live processes afterwards.
TEST stat $M0/file
TEST kill -0 $pid0
TEST kill -0 $pid1
TEST kill -0 $pid2
EXPECT "1" brick_up_status $V0 $H0 $B0/${V0}0
EXPECT "1" brick_up_status $V0 $H0 $B0/${V0}1
EXPECT "1" brick_up_status $V0 $H0 $B0/${V0}2

# A further change of the brick volfile on top of the failed one: a second
# in-process reload, same outcome. (A reset to the original volfile would not
# reload: the brick still runs, and still records, the original.)
TEST $CLI volume set $V0 delay-gen.enable finodelk
EXPECT_WITHIN $PROCESS_UP_TIMEOUT "^2$" listener_failed_count $(brick_log $B0/${V0}0) $base0
EXPECT_WITHIN $PROCESS_UP_TIMEOUT "^2$" listener_failed_count $(brick_log $B0/${V0}1) $base1
EXPECT_WITHIN $PROCESS_UP_TIMEOUT "^2$" listener_failed_count $(brick_log $B0/${V0}2) $base2
TEST stat $M0/file
TEST kill -0 $pid0
TEST kill -0 $pid1
TEST kill -0 $pid2
EXPECT "$pid0" get_brick_pid $V0 $H0 $B0/${V0}0
EXPECT "$pid1" get_brick_pid $V0 $H0 $B0/${V0}1
EXPECT "$pid2" get_brick_pid $V0 $H0 $B0/${V0}2

cleanup;
