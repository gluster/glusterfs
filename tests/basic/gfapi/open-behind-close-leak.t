#!/bin/bash
#
# gfapi released its fds without delivering fdclose to the graph, so with
# performance.open-behind on a glfs_open() followed by glfs_close() with no
# I/O in between left the fd_t alive (open-behind's deferred-open references)
# and pinned its inode for the life of the process. glfs_dup()ed handles share
# the fd_t, so the fdclose must come from the last handle only: the tester
# also opens, dups and closes N more files in both orders, reading through the
# dup after the original was closed.

. $(dirname $0)/../../include.rc
. $(dirname $0)/../../volume.rc

cleanup;

N=3
tester=$(dirname $0)/open-behind-close-leak
logfile=$LOGDIR/open-behind-close-leak.log
pidfile=$B0/open-behind-close-leak.pid

# Live fd_t objects created by the gfapi process: every fd_t owns exactly one
# gf_common_mt_fd_ctx array, accounted to the gfapi master xlator. This works on
# every build; the fd_t mem-pool count does not exist on tcmalloc builds.
function gfapi_live_fd_count {
        if [ ! -f "$1" ]; then
                echo "no-statedump"
                return
        fi
        awk -F= '/^\[mount\/api\.gfapi - usage-type gf_common_mt_fd_ctx memusage\]$/ {f=1}
                 f && /^num_allocs=/ {print $2; f=0; e=1} END {if (!e) print 0}' "$1"
}

TEST glusterd
TEST pidof glusterd
TEST $CLI volume create $V0 $H0:$B0/${V0}0
TEST $CLI volume set $V0 performance.open-behind on
TEST $CLI volume start $V0

# the files must exist before the gfapi process first sees their inodes;
# f1..fN for the plain pass, f(N+1)..f2N for the dup pass
TEST $GFS --volfile-id=/$V0 --volfile-server=$H0 $M0
for i in $(seq 1 $((2 * N))); do
        echo data > $M0/f$i
done
EXPECT_WITHIN $UMOUNT_TIMEOUT "Y" force_umount $M0

TEST build_tester $tester.c -lgfapi
rm -f $pidfile
TEST $tester $H0 $V0 $N $logfile $pidfile
pid=$(cat $pidfile 2>/dev/null)
TEST [ -n "$pid" ]
dump=$(ls $statedumpdir/glusterdump.$pid.dump.* 2>/dev/null | head -1)
TEST [ -n "$dump" ]

EXPECT "0" gfapi_live_fd_count "$dump"

rm -f $dump $pidfile
cleanup_tester $tester
cleanup;
