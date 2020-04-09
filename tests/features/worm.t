#!/bin/bash

. $(dirname $0)/../include.rc
. $(dirname $0)/../volume.rc

cleanup;

TEST glusterd
TEST pidof glusterd

TEST $CLI volume create $V0 $H0:$B0/${V0}1

EXPECT "$V0" volinfo_field $V0 'Volume Name'
EXPECT 'Created' volinfo_field $V0 'Status'

TEST $CLI volume start $V0
EXPECT 'Started' volinfo_field $V0 'Status'

## Mount FUSE with caching disabled (read-write)
TEST $GFS -s $H0 --volfile-id $V0 $M0

## Tests for the volume level WORM
TEST `echo "File 1" > $M0/file1`
TEST touch $M0/file2

## Enable the volume level WORM
TEST $CLI volume set $V0 features.worm 1
TEST ! mv $M0/file1 $M0/file11
TEST `echo "block" > $M0/file2`

## Disable the volume level WORM and delete the legacy files
TEST $CLI volume set $V0 features.worm 0
TEST rm -f $M0/*

## Enable file level WORM
TEST $CLI volume set $V0 features.worm-file-level 1
TEST $CLI volume set $V0 features.default-retention-period 10
TEST $CLI volume set $V0 features.auto-commit-period 5

## Tests for manual transition to WORM/Retained state
TEST `echo "worm 1" > $M0/file1`
TEST chmod 0444 $M0/file1
sleep 5
TEST `echo "line 1" > $M0/file1`
TEST ! mv $M0/file1 $M0/file2
sleep 10
TEST ! link $M0/file1 $M0/file2
sleep 5
TEST rm -f $M0/file1

## Test for mv over WORM/Reatined state
TEST `echo "worm 1" > $M0/file1`
TEST chmod 0444 $M0/file1
TEST `echo "worm 2" > $M0/file2`
TEST ! mv $M0/file2 $M0/file1
TEST rm -f $M0/file2
sleep 10
TEST rm -f $M0/file1

## Test for state transition over write.
TEST `echo "worm 1" > $M0/file3`
sleep 5
TEST `echo "worm 2" >> $M0/file3`
EXPECT 'worm 1' cat $M0/file3
TEST ! rm -f $M0/file3

## Test for checking if Worm files are undeletable after setting worm-files-deletable as 0.
TEST $CLI volume set $V0 features.worm-files-deletable 0
TEST `echo "worm 1" > $M0/file4`
TEST chmod 0444 $M0/file4
sleep 10
TEST `echo "worm 1" >> $M0/file4`
TEST ! rm -f $M0/file4

## Test for state transition if auto-commit-period is 0
TEST $CLI volume set $V0 features.auto-commit-period 0
TEST `echo "worm 1" > $M0/file5`
EXPECT '3/10/0' echo $(getfattr -e text --absolute-names --only-value -n "trusted.reten_state" $B0/${V0}1/file5)
EXPECT 'worm 1' cat $M0/file5
TEST ! rm -f $M0/file5
TEST $CLI volume set $V0 features.auto-commit-period 5

## Test for checking if retention-period is updated on increasing the access time of a WORM-RETAINED file.
TEST $CLI volume set $V0 features.worm-files-deletable 1
TEST `echo "worm 1" >> $M0/file1`
initial_timestamp=$(date +%s)
current_time_seconds=$(date +%S | sed 's/^0*//' );
TEST chmod 0444 $M0/file1
EXPECT '3/10/5' echo $(getfattr -e text --absolute-names --only-value -n "trusted.reten_state" $B0/${V0}1/file1)
changed_timestamp=$(date +%Y%m%d%H%M --date '60 seconds');
seconds_diff=`expr 60 - $((current_time_seconds))`
TEST `touch -a -t "${changed_timestamp}" $M0/file1`
EXPECT "3/$seconds_diff/5" echo $(getfattr -e text --absolute-names --only-value -n "trusted.reten_state" $B0/${V0}1/file1)
sleep $seconds_diff
TEST `echo "worm 2" >> $M0/file1`
EXPECT  "$initial_timestamp" echo $(stat --printf %X $M0/file1)


## Test for checking if retention-period is updated on decreasing the access time of a WORM-RETAINED file
TEST $CLI volume set $V0 features.default-retention-period 120
initial_timestamp=$(date +%s)
current_time_seconds=$(date +%S | sed 's/^0*//' );
TEST chmod 0444 $M0/file1
EXPECT '3/120/5' echo $(getfattr -e text --absolute-names --only-value -n "trusted.reten_state" $B0/${V0}1/file1)
changed_timestamp=$(date +%Y%m%d%H%M --date '60 seconds');
seconds_diff=`expr 60 - $((current_time_seconds))`
TEST `touch -a -t "${changed_timestamp}" $M0/file1`
EXPECT "3/$seconds_diff/5" echo $(getfattr -e text --absolute-names --only-value -n "trusted.reten_state" $B0/${V0}1/file1)
sleep $seconds_diff
TEST `echo "worm 4" >> $M0/file1`
EXPECT  "$initial_timestamp" echo $(stat --printf %X $M0/file1)
TEST rm -f $M0/file1

TEST $CLI volume stop $V0
EXPECT 'Stopped' volinfo_field $V0 'Status'

cleanup;
