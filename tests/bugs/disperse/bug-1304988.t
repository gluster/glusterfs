#!/bin/bash

. $(dirname $0)/../../include.rc
. $(dirname $0)/../../volume.rc

# This test renames files from dir to test and vice versa in an infinite loop
# at the same time add-brick and rebalance starts which should NOT be hanged

cleanup;

function rename_files {
while :
do
        for i in {1..100}; do mv $M0/dir/file-$i $M0/test/newfile-$i; done
        for i in {1..100}; do mv $M0/test/newfile-$i $M0/dir/file-$i; done
done
}

TEST glusterd
TEST pidof glusterd
TEST $CLI volume create $V0 disperse 6 redundancy 2 $H0:$B0/${V0}{0..5}
TEST $CLI volume start $V0
TEST glusterfs --entry-timeout=0 --attribute-timeout=0 -s $H0 --volfile-id $V0 $M0
EXPECT_WITHIN $CHILD_UP_TIMEOUT "6" ec_child_up_count $V0 0

mkdir $M0/dir
mkdir $M0/test
touch $M0/dir/file-{1..100}
rename_files &
back_pid1=$!;
echo "Started rename $back_pid1"

TEST $CLI volume add-brick $V0 $H0:$B0/${V0}{6..11}
TEST $CLI volume rebalance $V0 start force

#Test if rebalance completed with success.
EXPECT_WITHIN $REBALANCE_TIMEOUT "completed" rebalance_status_field $V0
echo "rebalance done..."
kill $back_pid1
cleanup;
#G_TESTDEF_TEST_STATUS_NETBSD7=BAD_TEST,BUG=1332022
#G_TESTDEF_TEST_STATUS_CENTOS6=BAD_TEST,BUG=1332022
