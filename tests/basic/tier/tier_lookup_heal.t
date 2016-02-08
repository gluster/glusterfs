#!/bin/bash

. $(dirname $0)/../../include.rc
. $(dirname $0)/../../volume.rc

LAST_BRICK=1
CACHE_BRICK_FIRST=2
CACHE_BRICK_LAST=3
PROMOTE_TIMEOUT=5

function file_on_fast_tier {
    local ret="1"

    s1=$(md5sum $1)
    s2=$(md5sum $B0/${V0}${CACHE_BRICK_FIRST}/$1)

    if [ -e $B0/${V0}${CACHE_BRICK_FIRST}/$1 ] && ! [ "$s1" == "$s2" ]; then
        echo "0"
    else
        echo "1"
    fi
}

cleanup


TEST glusterd
TEST pidof glusterd

TEST $CLI volume create $V0 replica 2 $H0:$B0/${V0}{0..$LAST_BRICK}
TEST $CLI volume start $V0
TEST $GFS --volfile-id=/$V0 --volfile-server=$H0 $M0;

# Create files before CTR xlator is on.
cd $M0
TEST stat .
TEST touch file1
TEST stat file1

# gf_file_tb and gf_flink_tb should be empty
ENTRY_COUNT=$(echo "select * from gf_file_tb; select * from gf_flink_tb;" | \
        sqlite3 $B0/${V0}$LAST_BRICK/.glusterfs/${V0}$LAST_BRICK.db | wc -l )
TEST [ $ENTRY_COUNT -eq 0 ]


#Attach tier and switch ON CTR Xlator.
TEST $CLI volume attach-tier $V0 replica 2 $H0:$B0/${V0}$CACHE_BRICK_FIRST $H0:$B0/${V0}$CACHE_BRICK_LAST
TEST $CLI volume set $V0 features.ctr-enabled on
TEST $CLI volume set $V0 cluster.tier-demote-frequency 4
TEST $CLI volume set $V0 cluster.tier-promote-frequency 4
TEST $CLI volume set $V0 cluster.read-freq-threshold 0
TEST $CLI volume set $V0 cluster.write-freq-threshold 0
TEST $CLI volume set $V0 performance.quick-read off
TEST $CLI volume set $V0 performance.io-cache off
TEST $CLI volume set $V0 cluster.tier-mode test

#The lookup should heal the database.
TEST ls file1

# gf_file_tb and gf_flink_tb should NOT be empty
ENTRY_COUNT=$(echo "select * from gf_file_tb; select * from gf_flink_tb;" | \
        sqlite3 $B0/${V0}$LAST_BRICK/.glusterfs/${V0}$LAST_BRICK.db | wc -l )
TEST [ $ENTRY_COUNT -eq 2 ]

# Heat-up the file
uuidgen > file1
sleep 5

#Check if the file is promoted
EXPECT_WITHIN $PROMOTE_TIMEOUT "0" file_on_fast_tier file1

cd;

cleanup
#G_TESTDEF_TEST_STATUS_NETBSD7=BAD_TEST,BUG=000000
#G_TESTDEF_TEST_STATUS_CENTOS6=BAD_TEST,BUG=000000
