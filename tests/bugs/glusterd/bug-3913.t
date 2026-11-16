#!/bin/bash

# Regression test for https://github.com/gluster/glusterfs/issues/3913
#
# Repeated "gluster volume status <vol> detail" commands used to leak one
# runner argv buffer in glusterd whenever the brick filesystem has no
# inode-size query tool (e.g. btrfs/zfs, or an unknown filesystem).  The
# leaked allocations show up as live `gf_common_mt_run_argv` allocations in
# glusterd's statedump.  They must be zero after the loop below.

. $(dirname $0)/../include.rc
. $(dirname $0)/../volume.rc

cleanup;

TEST glusterd
TEST pidof glusterd

TEST $CLI volume create $V0 $H0:$B0/${V0}0
TEST $CLI volume start $V0

# Exercise the `detail` specific code path (glusterd_add_inode_size_to_dict)
# several times.
for i in $(seq 1 20); do
        $CLI volume status $V0 detail >/dev/null 2>&1
done

gd_pid=$(pidof glusterd | awk '{print $1}')
sdump=$(generate_statedump $gd_pid)
TEST [ "$sdump" != "nostatedump" ]

# A live run_argv entry means a runner was initialized but never ended.
runargv=$(awk '/usage-type gf_common_mt_run_argv memusage/ {getline; print $2}' \
          $sdump)
EXPECT "" echo "$runargv"

cleanup;
