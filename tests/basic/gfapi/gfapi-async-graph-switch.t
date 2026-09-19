#!/bin/bash
#
# An asynchronous gfapi fop that completes after a graph switch must release
# the fd_t it was wound on. glfs_io_async_cbk() used to release glfd->fd, the
# handle's fd_t at completion time, which a graph switch during the fop had
# swapped to the new graph's fd_t: the handle's own reference was dropped (the
# next read and the close fail with EBADF) and the old graph's fd_t leaked.
#
# The brick holds READ for 10 s (debug.delay-gen) so that a client-side option
# change, issued while one asynchronous read is in flight, switches the graph
# before the read completes.

. $(dirname $0)/../../include.rc
. $(dirname $0)/../../volume.rc

cleanup;

TEST glusterd
TEST pidof glusterd

TEST $CLI volume create $V0 $H0:$B0/brick1;
EXPECT 'Created' volinfo_field $V0 'Status';

# the read must reach the brick and be answered from there
TEST $CLI volume set $V0 performance.quick-read off
TEST $CLI volume set $V0 performance.io-cache off
TEST $CLI volume set $V0 performance.read-ahead off
TEST $CLI volume set $V0 performance.open-behind off

# hold every READ on the brick for 10 s
TEST $CLI volume set $V0 debug.delay-gen posix
TEST $CLI volume set $V0 delay-gen.enable read
TEST $CLI volume set $V0 delay-gen.delay-percentage 100
TEST $CLI volume set $V0 delay-gen.delay-duration 10000000

TEST $CLI volume start $V0;
EXPECT 'Started' volinfo_field $V0 'Status';

logdir=`gluster --print-logdir`

TEST build_tester $(dirname $0)/gfapi-async-graph-switch.c -lgfapi
# the option change below is what switches the client graph mid-read
TEST ./$(dirname $0)/gfapi-async-graph-switch $H0 $V0 \
    $logdir/gfapi-async-graph-switch.log \
    $CLI volume set $V0 performance.stat-prefetch off

cleanup_tester $(dirname $0)/gfapi-async-graph-switch

TEST $CLI volume stop $V0
TEST $CLI volume delete $V0

cleanup;
