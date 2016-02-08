#!/bin/bash

. $(dirname $0)/../../include.rc
. $(dirname $0)/../../volume.rc

LAST_BRICK=3
CACHE_BRICK_FIRST=4
CACHE_BRICK_LAST=5

cleanup

# Start glusterd [1-2]
TEST glusterd
TEST pidof glusterd

# Set-up tier cluster [3-4]
TEST $CLI volume create $V0 replica 2 $H0:$B0/${V0}{0..$LAST_BRICK}
TEST $CLI volume start $V0
TEST $CLI volume attach-tier $V0 replica 2 $H0:$B0/${V0}$CACHE_BRICK_FIRST $H0:$B0/${V0}$CACHE_BRICK_LAST

# Start and mount the volume after enabling CTR and trash [5-8]
TEST $CLI volume set $V0 features.ctr-enabled on
TEST $CLI volume set $V0 features.trash on
TEST $GFS --volfile-id=/$V0 --volfile-server=$H0 $M0;

# Create an empty file
touch $M0/foo

# gf_file_tb and gf_flink_tb should contain one entry each [9]
ENTRY_COUNT=$(echo "select * from gf_file_tb; select * from gf_flink_tb;" | \
        sqlite3 $B0/${V0}5/.glusterfs/${V0}5.db | wc -l )
TEST [ $ENTRY_COUNT -eq 2 ]

# Create two hard links
ln $M0/foo $M0/lnk1
ln $M0/foo $M0/lnk2

# Now gf_flink_tb should contain 3 entries [10]
ENTRY_COUNT=$(echo "select * from gf_flink_tb;" | \
        sqlite3 $B0/${V0}5/.glusterfs/${V0}5.db | wc -l )
TEST [ $ENTRY_COUNT -eq 3 ]

# Delete the hard link
rm -rf $M0/lnk1

# Corresponding hard link entry must be removed from gf_flink_tb
# but gf_file_tb should still contain the file entry [11]
ENTRY_COUNT=$(echo "select * from gf_file_tb; select * from gf_flink_tb;" | \
        sqlite3 $B0/${V0}5/.glusterfs/${V0}5.db | wc -l )
TEST [ $ENTRY_COUNT -eq 3 ]

# Remove the file
rm -rf $M0/foo

# Another hardlink removed [12]
ENTRY_COUNT=$(echo "select * from gf_file_tb; select * from gf_flink_tb;" | \
        sqlite3 $B0/${V0}5/.glusterfs/${V0}5.db | wc -l )
TEST [ $ENTRY_COUNT -eq 2 ]

# Remove the last hardlink
rm -rf $M0/lnk2

# All entried must be removed from gf_flink_tb and gf_file_tb [13]
ENTRY_COUNT=$(echo "select * from gf_file_tb; select * from gf_flink_tb;" | \
        sqlite3 $B0/${V0}5/.glusterfs/${V0}5.db | wc -l )
TEST [ $ENTRY_COUNT -eq 0 ]

cleanup



#G_TESTDEF_TEST_STATUS_NETBSD7=KNOWN_ISSUE,BUG=000000
