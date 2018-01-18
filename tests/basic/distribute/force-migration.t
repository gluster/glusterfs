#!/bin/bash

. $(dirname $0)/../../include.rc
. $(dirname $0)/../../volume.rc

#This tests checks if the file migration fails with force-migration
#option set to off.

cleanup;

TEST glusterd
TEST pidof glusterd
TEST $CLI volume create $V0 $H0:$B0/${V0}0 $H0:$B0/${V0}1
TEST $CLI volume set $V0 performance.quick-read off
TEST $CLI volume set $V0 performance.io-cache off
TEST $CLI volume set $V0 performance.write-behind off
TEST $CLI volume set $V0 performance.stat-prefetch off
TEST $CLI volume set $V0 performance.read-ahead off
TEST $CLI volume start $V0
TEST glusterfs --volfile-id=/$V0 --volfile-server=$H0 $M0 --attribute-timeout=0 --entry-timeout=0
TEST touch $M0/file
#This rename creates a link file for tile in the other brick.
TEST mv $M0/file $M0/tile
#Lets keep writing to the file which will have a open fd
dd if=/dev/zero of=$M0/tile bs=1b &
bg_pid=$!
#Now rebalance will try to skip the file
TEST $CLI volume set $V0 force-migration off
TEST $CLI volume rebalance $V0 start force
EXPECT_WITHIN $REBALANCE_TIMEOUT "completed" rebalance_status_field $V0
skippedcount=`gluster v rebalance $V0 status | awk 'NR==3{print $6}'`
TEST [[ $skippedcount -eq 1 ]]
#file should be migrated now
TEST $CLI volume set $V0 force-migration on
TEST $CLI volume rebalance $V0 start force
EXPECT_WITHIN $REBALANCE_TIMEOUT "completed" rebalance_status_field $V0
skippedcount=`gluster v rebalance $V0 status | awk 'NR==3{print $6}'`
rebalancedcount=`gluster v rebalance $V0 status | awk 'NR==3{print $2}'`
TEST [[ $skippedcount -eq 0 ]]
TEST [[ $rebalancedcount -eq 1 ]]
kill -9 $bg_pid > /dev/null 2>&1
wait > /dev/null 2>&1
cleanup
#Bad test because we are not sure writes are happening at the time of
#rebalance. We need to write a test case which makes sure client
#writes happen during rebalance. One way would be to set S+T bits on
#src and write to file from client and then start rebalance. Currently
#marking this as bad test.
#G_TESTDEF_TEST_STATUS_CENTOS6=BAD_TEST,BUG=000000

