#!/bin/bash

. $(dirname $0)/../../include.rc
. $(dirname $0)/../../volume.rc
. $(dirname $0)/../../tier.rc

LAST_BRICK=1
CACHE_BRICK_FIRST=4
CACHE_BRICK_LAST=5

DEMOTE_FREQ=5
PROMOTE_FREQ=5

cleanup

# Start glusterd
TEST glusterd
TEST pidof glusterd

# Set-up tier cluster
TEST $CLI volume create $V0 replica 2 $H0:$B0/${V0}{0..$LAST_BRICK}
TEST $CLI volume start $V0
TEST $CLI volume attach-tier $V0 replica 2 $H0:$B0/${V0}$CACHE_BRICK_FIRST $H0:$B0/${V0}$CACHE_BRICK_LAST

TEST $CLI volume set $V0 cluster.tier-demote-frequency $DEMOTE_FREQ
TEST $CLI volume set $V0 cluster.tier-promote-frequency $PROMOTE_FREQ

# Start and mount the volume after enabling CTR
TEST $CLI volume set $V0 features.ctr-enabled on
TEST $GFS --volfile-id=/$V0 --volfile-server=$H0 $M0;

# create two files
echo "hello world" > $M0/file1
echo "hello world" > $M0/file2

# db in hot brick shows 4 record. 2 for file1 and 2 for file2
ENTRY_COUNT=$(echo "select * from gf_file_tb; select * from gf_flink_tb;" | \
        sqlite3 $B0/${V0}5/.glusterfs/${V0}5.db | wc -l )
TEST [ $ENTRY_COUNT -eq 4 ]

#overwrite file2 with file1
mv -f $M0/file1 $M0/file2

# Now the db in hot tier should have only 2 records for file1.
ENTRY_COUNT=$(echo "select * from gf_file_tb; select * from gf_flink_tb;" | \
        sqlite3 $B0/${V0}5/.glusterfs/${V0}5.db | wc -l )
TEST [ $ENTRY_COUNT -eq 2 ]

cleanup
#G_TESTDEF_TEST_STATUS_NETBSD7=KNOWN_ISSUE,BUG=000000
