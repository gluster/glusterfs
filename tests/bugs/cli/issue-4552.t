#!/bin/bash

. $(dirname $0)/../../include.rc
. $(dirname $0)/../../volume.rc

cleanup;

# The CLI serves RPC replies up to 128 KiB from small allocations and does
# not need ctx->iobuf_pool for them.  Bigger replies go through
# iobuf_get2() -> iobuf_get_from_stdalloc(), which dereferences the pool.
# The CLI never initialized that pool, so "volume status" of a volume with
# enough bricks crashed with a NULL pool (github.com/gluster/glusterfs#4552).
#
# 600 bricks are plenty to push the status reply past the 128 KiB limit.
BRICK_COUNT=600

TEST glusterd
TEST pidof glusterd

# Keep the number of brick processes low so this test is cheap to run.
TEST $CLI volume set all cluster.brick-multiplex on
TEST $CLI volume set all cluster.max-bricks-per-process 100

bricks=""
for i in $(seq 1 $BRICK_COUNT); do
        mkdir -p $B0/${V0}_$i
        bricks="$bricks $H0:$B0/${V0}_$i"
done

TEST $CLI volume create $V0 $bricks force
TEST $CLI volume start $V0

# Without the fix the CLI segfaults here instead of printing the status.
TEST $CLI volume status $V0

TEST $CLI volume stop $V0 force

cleanup;
