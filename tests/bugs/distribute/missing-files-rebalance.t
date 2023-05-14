#!/bin/bash

. $(dirname $0)/../../include.rc
. $(dirname $0)/../../volume.rc

cleanup;

TESTS_EXPECTED_IN_LOOP=100

TEST glusterd
TEST pidof glusterd
TEST $CLI volume info

#Create a distributed volume
TEST $CLI volume create $V0 $H0:$B0/${V0}{1..2};
TEST $CLI volume start $V0

# Mount FUSE
TEST glusterfs -s $H0 --volfile-id $V0 $M0

#Create files
TEST mkdir $M0/dir
TEST touch $M0/dir/foo{1..100}
for i in {1..100}; do
    TEST_IN_LOOP mv $M0/dir/foo$i $M0/dir/new$i;
done

TEST $CLI volume add-brick $V0 $H0:$B0/${V0}3 $H0:$B0/${V0}4
TEST $CLI volume rebalance $V0 start
EXPECT_WITHIN $REBALANCE_TIMEOUT "completed" rebalance_status_field $V0

#Check if the mount contains all the files after rebalance
numFiles=(`ls $M0/dir/|grep -v '^\.'| wc -l`)
#Check if all the files has the same permission
wrongPerm=(`ls -l $M0/dir/| grep -v "\-rw\-r" | grep -v "total" | wc -l`)
EXPECT "^100$" echo $numFiles
EXPECT "^0$" echo $wrongPerm

cleanup;
