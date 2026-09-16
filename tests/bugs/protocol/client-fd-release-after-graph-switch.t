#!/bin/bash
#
# An fd left open across a graph switch lives on the old graph until the
# application closes it. By then the old graph has received PARENT_DOWN and
# its protocol/client has marked every saved fd bad (remote_fd == -1), and
# client4_0_release() deferred the destruction of the fd ctx to a reopen that
# can never run on that graph: one clnt_fd_ctx_t and the fd_lk_ctx_t reference
# it holds leaked per fd for the life of the mount.

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

# Live fd lock contexts: fd_create() gives every fd_t one fd_lk_ctx_t,
# accounted to the fuse xlator; a migrated fd shares the base fd's. The
# section is only printed when the count is non-zero.
function fuse_lk_ctx_count {
        local sd=$(generate_mount_statedump $V0 $M0)
        awk -F= '/^\[mount\/fuse\.fuse - usage-type gf_common_mt_fd_lk_ctx_t memusage\]$/ {f=1}
                 f && /^num_allocs=/ {print $2; f=0; e=1} END {if (!e) print 0}' "$sd"
        rm -f "$sd"
}

# Live fd_t objects created by the fuse client (one gf_common_mt_fd_ctx array
# each): the control that every application handle really was released.
function fuse_live_fd_count {
        local sd=$(generate_mount_statedump $V0 $M0)
        awk -F= '/^\[mount\/fuse\.fuse - usage-type gf_common_mt_fd_ctx memusage\]$/ {f=1}
                 f && /^num_allocs=/ {print $2; f=0; e=1} END {if (!e) print 0}' "$sd"
        rm -f "$sd"
}

TEST glusterd
TEST pidof glusterd
TEST $CLI volume create $V0 $H0:$B0/${V0}0
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

# write through the handles after the switch: a writev reaches the new
# graph's open-behind and triggers the deferred open it kept for the
# migrated fd, so that fd is released normally on close and nothing but
# the old graph's saved fd ctx can hold anything afterwards
for i in $(seq 1 $N); do
        eval "echo more >&$((10 + i))"
done

# close them
for i in $(seq 1 $N); do
        eval "exec $((10 + i))>&-"
done

EXPECT_WITHIN $PROCESS_UP_TIMEOUT "0" fuse_live_fd_count
EXPECT_WITHIN $PROCESS_UP_TIMEOUT "0" fuse_lk_ctx_count

cleanup;
