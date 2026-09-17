#!/bin/bash
#
# glfs_fini() of a libgfapi client that has done I/O must complete well within
# the 10 s call_bail period. Regression guard for the teardown-time timer-cancel
# stall (#4320): when gf_timer_call_cancel() refuses to cancel during ctx cleanup
# and the caller releases the timer's rpc_clnt ref only on a successful cancel,
# glfs_fini() waits for the bail event to fire (~11 s measured). The fixed path
# takes ~0.1-1.1 s (the pool-drain countdown); 5 s splits the two by a wide margin.

. $(dirname $0)/../../include.rc
. $(dirname $0)/../../volume.rc

cleanup;

TEST glusterd

TEST $CLI volume create $V0 $H0:$B0/brick1;
EXPECT 'Created' volinfo_field $V0 'Status';

TEST $CLI volume start $V0;
EXPECT 'Started' volinfo_field $V0 'Status';

logdir=`gluster --print-logdir`

TEST build_tester $(dirname $0)/glfs_fini-timing.c -lgfapi

rm -f $logdir/glfs_fini-timing.log
ms=$(./$(dirname $0)/glfs_fini-timing $H0 $V0 $logdir/glfs_fini-timing.log)
rc=$?
echo "# glfs_fini took ${ms:-?} ms, exit $rc"
TEST [ "$rc" -eq 0 ]
TEST [ -n "$ms" ]
TEST [ "$ms" -lt 5000 ]

# Deterministic tripwire for the exact regression: a cancel that refuses during
# ctx cleanup logs "[timer.c:NN:gf_timer_call_cancel] ... ctx cleanup started" at
# INFO. Qualified by the emitting function because gf_timer_call_after's refusal
# message contains the same words. The log is removed before the run so the
# positive control speaks for this run. (TEST word-splits its arguments, so the
# quoted pattern lives in a function.)
function refused_cancel_logged {
    grep -q "gf_timer_call_cancel.*ctx cleanup started" "$1"
}
TEST [ -s $logdir/glfs_fini-timing.log ]
TEST ! refused_cancel_logged $logdir/glfs_fini-timing.log

cleanup_tester $(dirname $0)/glfs_fini-timing

TEST $CLI volume stop $V0
TEST $CLI volume delete $V0

cleanup;
