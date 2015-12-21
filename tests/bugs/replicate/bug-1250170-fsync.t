#!/bin/bash

. $(dirname $0)/../../include.rc
. $(dirname $0)/../../volume.rc

cleanup;
TEST gcc  $(dirname $0)/bug-1250170-fsync.c -o  $(dirname $0)/bug-1250170-fsync
TEST glusterd
TEST pidof glusterd

TEST $CLI volume create $V0 replica 2 $H0:$B0/${V0}{0,1}
TEST $CLI volume set $V0 performance.write-behind off
TEST $CLI volume start $V0
TEST $GFS --volfile-id=$V0 --volfile-server=$H0 $M0;

TEST touch $M0/file
TEST kill_brick $V0 $H0 $B0/${V0}0
TEST gluster volume profile $V0 start
#Perform 5 non-sequential writes.
TEST $(dirname $0)/bug-1250170-fsync $M0/file

#Run profile info initially to filter out the interval statistics in the
#subsequent runs.
TEST $CLI volume profile $V0 info
#We get only cumulative statistics.
write_count=$($CLI volume profile $V0 info | grep WRITE |awk '{count += $8} END {print count}')
fsync_count=$($CLI volume profile $V0 info | grep FSYNC |awk '{count += $8} END {print count}')

EXPECT "5" echo $write_count
TEST [ -z $fsync_count ]

TEST $CLI volume profile $V0 stop
TEST umount $M0
rm -f $(dirname $0)/bug-1250170-fsync
cleanup
