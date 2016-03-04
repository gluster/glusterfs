#!/bin/bash

. $(dirname $0)/../../include.rc
. $(dirname $0)/../../volume.rc

cleanup

function write_to_file {
        dd of=$M0/dir/file if=/dev/urandom bs=1024k count=128 2>&1 >/dev/null
}

TEST glusterd
TEST pidof glusterd
TEST $CLI volume create $V0 replica 2 $H0:$B0/${V0}0 $H0:$B0/${V0}1

TEST $CLI volume set $V0 cluster.eager-lock on
TEST $CLI volume set $V0 post-op-delay-secs 3
TEST $CLI volume set $V0 cluster.entry-self-heal off
TEST $CLI volume set $V0 cluster.data-self-heal off
TEST $CLI volume set $V0 cluster.metadata-self-heal off
TEST $CLI volume set $V0 cluster.self-heal-daemon off

TEST $CLI volume start $V0
TEST $CLI volume profile $V0 start
TEST $CLI volume set $V0 ensure-durability off
TEST glusterfs --volfile-id=/$V0 --volfile-server=$H0 $M0 --attribute-timeout=0 --entry-timeout=0
TEST mkdir $M0/dir
TEST touch $M0/dir/file

TEST kill_brick $V0 $H0 $B0/${V0}0
TEST `echo 'abc' > $M0/dir/file`

TEST $CLI volume start $V0 force

EXPECT_WITHIN $PROCESS_UP_TIMEOUT "1" afr_child_up_status $V0 0
EXPECT_WITHIN $PROCESS_UP_TIMEOUT "1" afr_child_up_status $V0 1

write_to_file &
#Test if the MAX [F]INODELK fop latency is of the order of seconds.
EXPECT "^1$" get_pending_heal_count $V0
inodelk_max_latency=$($CLI volume profile $V0 info | grep INODELK | awk 'BEGIN {max = 0} {if ($6 > max) max=$6;} END {print max}' | cut -d. -f 1 | egrep "[0-9]{7,}")
TEST [ -z $inodelk_max_latency ]
cleanup
