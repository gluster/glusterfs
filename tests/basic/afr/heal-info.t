#!/bin/bash
#Test that parallel heal-info command execution doesn't result in spurious
#entries with locking-scheme granular

. $(dirname $0)/../../include.rc
. $(dirname $0)/../../volume.rc

cleanup;

function heal_info_to_file {
        while [ -f $M0/b.txt ]; do
                $CLI volume heal $V0 info | grep -i number | grep -v 0 >> $1
        done
}

function write_and_del_file {
        dd of=$M0/a.txt if=/dev/zero bs=1024k count=100
        rm -f $M0/b.txt
}

TEST glusterd
TEST pidof glusterd
TEST $CLI volume create $V0 replica 2 $H0:$B0/brick{0,1}
TEST $CLI volume set $V0 locking-scheme granular
TEST $CLI volume start $V0
TEST $GFS --volfile-id=$V0 --volfile-server=$H0 $M0;
TEST touch $M0/a.txt $M0/b.txt
write_and_del_file &
touch $B0/f1 $B0/f2
heal_info_to_file $B0/f1 &
heal_info_to_file $B0/f2 &
wait
EXPECT "^$" cat $B0/f1
EXPECT "^$" cat $B0/f2

cleanup;
