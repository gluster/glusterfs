#!/bin/bash
#
# An fd held open across a graph switch and then closed without any further
# fd fop leaked its new-graph fd_t and fuse fd ctx (and pinned the inode) when
# performance.open-behind is on: fuse_release() delivered fdclose to the base
# fd's graph only, so the new graph's open-behind never cancelled the deferred
# open it kept for the migrated fd.

. $(dirname $0)/../../include.rc
. $(dirname $0)/../../volume.rc

cleanup;

N=5

function mount_log {
        ls $($CLI --print-logdir)/mnt-glusterfs-0.log 2>/dev/null | head -1
}

# the mount log persists across tests: every count below is relative to a baseline
function switched_count {
        echo $(( $(grep -c "switched to graph" "$(mount_log)") - ${1:-0} ))
}

function migrated_count {
        echo $(( $(grep -c "migrated basefd" "$(mount_log)") - ${1:-0} ))
}

# Live fd_t objects created by the fuse client: every fd_t owns exactly one
# gf_common_mt_fd_ctx array, accounted to the fuse xlator. This exists on every
# build; the fd_t mem-pool count does not exist on tcmalloc builds.
function fuse_live_fd_count {
        local sd=$(generate_mount_statedump $V0 $M0)
        awk -F= '/^\[mount\/fuse\.fuse - usage-type gf_common_mt_fd_ctx memusage\]$/ {f=1}
                 f && /^num_allocs=/ {print $2; f=0; e=1} END {if (!e) print 0}' "$sd"
        rm -f "$sd"
}

# num_allocs of gf_fuse_mt_fd_ctx_t (the section is only printed when non-zero)
function fuse_fd_ctx_count {
        local sd=$(generate_mount_statedump $V0 $M0)
        awk -F= '/gf_fuse_mt_fd_ctx_t memusage/ {f=1} f && /^num_allocs=/ {print $2; f=0; e=1} END {if (!e) print 0}' "$sd"
        rm -f "$sd"
}

TEST glusterd
TEST pidof glusterd
TEST $CLI volume create $V0 $H0:$B0/${V0}0
TEST $CLI volume set $V0 performance.open-behind on
TEST $CLI volume start $V0
TEST $GFS --volfile-id=/$V0 --volfile-server=$H0 $M0

for i in $(seq 1 $N); do
        echo data > $M0/f$i
done
s0=$(switched_count)
m0=$(migrated_count)

# keep the files open (O_RDWR) across the switch
for i in $(seq 1 $N); do
        eval "exec $((10 + i))<>$M0/f$i"
done

# a graph switch; fuse performs it on the next request, so poke the mount
TEST $CLI volume set $V0 performance.stat-prefetch off
TEST stat $M0
EXPECT_WITHIN $GRAPH_SWITCH_TIMEOUT "1" switched_count $s0
EXPECT_WITHIN $GRAPH_SWITCH_TIMEOUT "$N" migrated_count $m0

# close without any other fop on the handles
for i in $(seq 1 $N); do
        eval "exec $((10 + i))>&-"
done

EXPECT_WITHIN $PROCESS_UP_TIMEOUT "0" fuse_live_fd_count
EXPECT_WITHIN $PROCESS_UP_TIMEOUT "0" fuse_fd_ctx_count

cleanup;
