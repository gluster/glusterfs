#!/bin/bash

. $(dirname $0)/../../include.rc
. $(dirname $0)/../../volume.rc

# This test checks if there are any open fds on the brick
# even after the file is closed on the mount. This particular
# test tests dd with "fsync" to check afr's fsync codepath
cleanup;

function is_fd_open {
        local v=$1
        local h=$2
        local b=$3
        local bpid=$(get_brick_pid $v $h $b)
        ls -l /proc/$bpid/fd | grep -w "\-> $b/1"
}

TEST glusterd
TEST pidof glusterd
TEST $CLI volume create $V0 replica 2 $H0:$B0/${V0}{0,1}
TEST $CLI volume set $V0 ensure-durability off
TEST $CLI volume set $V0 cluster.eager-lock off
TEST $CLI volume set $V0 flush-behind off
TEST $CLI volume start $V0
TEST glusterfs --volfile-id=/$V0 --volfile-server=$H0 $M0 --attribute-timeout=0 --entry-timeout=0
TEST dd of=$M0/1 if=/dev/zero bs=1k count=1 conv=fsync
TEST ! is_fd_open $V0 $H0 $B0/${V0}0
cleanup;
