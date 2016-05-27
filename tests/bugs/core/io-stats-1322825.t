#!/bin/bash

# Test details:
# This is to test that the io-stat-dump xattr is not set on the brick,
# against the path that is used to trigger the stats dump.
# Additionally it also tests if as many io-stat dumps are generated as there
# are io-stat xlators in the graphs, which is 2 by default

. $(dirname $0)/../../include.rc

cleanup;

TEST glusterd
TEST pidof glusterd

# Covering replication and distribution in the test
TEST $CLI volume create $V0 replica 2 $H0:$B0/${V0}{1..4}
TEST $CLI volume start $V0
TEST $GFS -s $H0 --volfile-id $V0 $M0

# Generate some activity for the stats to produce something useful
TEST $CLI volume profile $V0 start
TEST mkdir $M0/dir1

# Generate the stat dump across the io-stat instances
TEST setfattr -n trusted.io-stats-dump -v /tmp/io-stats-1322825 $M0

# Check if $M0 is clean w.r.t xattr information
# TODO: if there are better ways to check we really get no attr error, please
# correct the following.
getfattr -n trusted.io-stats-dump $B0/${V0}1 2>&1 | grep -qi "no such attribute"
ret=$(echo $?)
EXPECT 0 echo $ret
getfattr -n trusted.io-stats-dump $B0/${V0}2 2>&1 | grep -qi "no such attribute"
ret=$(echo $?)
EXPECT 0 echo $ret
getfattr -n trusted.io-stats-dump $B0/${V0}3 2>&1 | grep -qi "no such attribute"
ret=$(echo $?)
EXPECT 0 echo $ret
getfattr -n trusted.io-stats-dump $B0/${V0}4 2>&1 | grep -qi "no such attribute"
ret=$(echo $?)
EXPECT 0 echo $ret

# Check if we have 5 io-stat files in /tmp
EXPECT 5 ls -1 /tmp/io-stats-1322825*
# Cleanup the 5 generated files
rm -f /tmp/io-stats-1322825*

# Rinse and repeat above for a directory
TEST setfattr -n trusted.io-stats-dump -v /tmp/io-stats-1322825 $M0/dir1
getfattr -n trusted.io-stats-dump $B0/${V0}1/dir1 2>&1 | grep -qi "no such attribute"
ret=$(echo $?)
EXPECT 0 echo $ret
getfattr -n trusted.io-stats-dump $B0/${V0}2/dir1 2>&1 | grep -qi "no such attribute"
ret=$(echo $?)
EXPECT 0 echo $ret
getfattr -n trusted.io-stats-dump $B0/${V0}3/dir1 2>&1 | grep -qi "no such attribute"
ret=$(echo $?)
EXPECT 0 echo $ret
getfattr -n trusted.io-stats-dump $B0/${V0}4/dir1 2>&1 | grep -qi "no such attribute"
ret=$(echo $?)
EXPECT 0 echo $ret

EXPECT 5 ls -1 /tmp/io-stats-1322825*
rm -f /tmp/io-stats-1322825*

cleanup;
