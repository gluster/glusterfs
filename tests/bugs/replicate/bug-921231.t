#!/bin/bash

. $(dirname $0)/../../include.rc
. $(dirname $0)/../../volume.rc

# This test writes to same file with 2 fds and tests that cluster.eager-lock is not
# causing extra delay because of post-op-delay-secs
cleanup;

function write_to_file {
        dd of=$M0/1 if=/dev/zero bs=1024k count=128 oflag=append 2>&1 >/dev/null
}

TEST glusterd
TEST pidof glusterd
TEST $CLI volume create $V0 replica 2 $H0:$B0/${V0}0 $H0:$B0/${V0}1
TEST $CLI volume set $V0 cluster.eager-lock on
TEST $CLI volume set $V0 post-op-delay-secs 3
TEST $CLI volume set $V0 client-log-level DEBUG
TEST $CLI volume start $V0
TEST $CLI volume profile $V0 start
TEST $CLI volume set $V0 ensure-durability off
TEST glusterfs --volfile-id=/$V0 --volfile-server=$H0 $M0 --attribute-timeout=0 --entry-timeout=0
write_to_file &
write_to_file &
wait
#Test if the MAX [F]INODELK fop latency is of the order of seconds.
inodelk_max_latency=$($CLI volume profile $V0 info | grep INODELK | awk 'BEGIN {max = 0} {if ($6 > max) max=$6;} END {print max}' | cut -d. -f 1 | egrep "[0-9]{7,}")
TEST [ -z $inodelk_max_latency ]

cleanup;
