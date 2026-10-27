#!/bin/bash
#
# A gfapi fd that holds a POSIX lock when the client switches graphs is
# migrated by glfs_migrate_fd_safe(). It gave the new fd the old fd's lock
# context only after opening it on the new graph, so the new graph's
# protocol/client kept the new fd's own, empty context: it believed the fd
# held no lock (wrong client.strict-locks and reopen decisions), and that
# context leaked on close. The tester locks a file, switches the graph,
# takes a statedump with the lock held and another after the close.

. $(dirname $0)/../../include.rc
. $(dirname $0)/../../volume.rc

cleanup;

tester=$(dirname $0)/migrate-fd-lock-context
logfile=$LOGDIR/migrate-fd-lock-context.log
pidfile=$B0/migrate-fd-lock-context.pid

# Locks protocol/client lists for its open fds (client_fd_lk_ctx_dump()), in
# the active graph only: the client that serves the handle after the switch.
function active_client_lock_count {
        awk '/^\[active graph - / {a=1; next} /^\[oldgraph\[/ {a=0}
             a && /^granted-posix-lock\[/ {n++} END {print n + 0}' "$1"
}

# Live gfapi objects of one memory type, accounted to the gfapi master
# xlator: fd_t (one gf_common_mt_fd_ctx array each) and fd lock contexts.
function gfapi_live_count {
        awk -F= -v t="$2" '$0 == "[mount/api.gfapi - usage-type " t " memusage]" {f=1}
                 f && /^num_allocs=/ {print $2; f=0; e=1} END {if (!e) print 0}' "$1"
}

TEST glusterd
TEST pidof glusterd
TEST $CLI volume create $V0 $H0:$B0/${V0}0
# no deferred opens: the old graph must release the old fd at the migration
TEST $CLI volume set $V0 performance.open-behind off
TEST $CLI volume start $V0

TEST build_tester $tester.c -lgfapi
rm -f $pidfile
# the option change below is what switches the client graph
TEST $tester $H0 $V0 $logfile $pidfile \
    $CLI volume set $V0 performance.stat-prefetch off
pid=$(cat $pidfile 2>/dev/null)
TEST [ -n "$pid" ]
dumps=($(ls -tr $statedumpdir/glusterdump.$pid.dump.* 2>/dev/null))
TEST [ ${#dumps[@]} -eq 2 ]

# with the lock held, after the switch
EXPECT "1" active_client_lock_count "${dumps[0]}"
# after the close: no fd left, and no lock context
EXPECT "0" gfapi_live_count "${dumps[1]}" gf_common_mt_fd_ctx
EXPECT "0" gfapi_live_count "${dumps[1]}" gf_common_mt_fd_lk_ctx_t

rm -f "${dumps[@]}" $pidfile
cleanup_tester $tester
cleanup;
