#!/bin/bash

. $(dirname $0)/../../include.rc
. $(dirname $0)/../../volume.rc

#This script tests that self-heal of limit-set xattr is happening on a directory
#but self-heal of quota.size xattr is not happening

cleanup;

TEST glusterd
TEST $CLI volume create $V0 replica 2 $H0:$B0/${V0}{0,1}
TEST $CLI volume start $V0
#Lets disable perf-xls so that lookup would reach afr
TEST $CLI volume set $V0 performance.quick-read off
TEST $CLI volume set $V0 performance.io-cache off
TEST $CLI volume set $V0 performance.write-behind off
TEST $CLI volume set $V0 performance.stat-prefetch off
TEST $CLI volume set $V0 performance.read-ahead off
TEST $CLI volume set $V0 self-heal-daemon off
TEST $CLI volume quota $V0 enable

TEST kill_brick $V0 $H0 $B0/${V0}0
TEST glusterfs --volfile-id=/$V0 --volfile-server=$H0 $M0 --attribute-timeout=0 --entry-timeout=0
cd $M0
TEST mkdir $M0/a
TEST $CLI volume quota $V0 limit-usage /a 1GB
echo abc > $M0/a/f
$CLI volume start $V0 force
EXPECT_WITHIN $CHILD_UP_TIMEOUT "1" afr_child_up_status $V0 0
quota_limit_val1=$(get_hex_xattr trusted.glusterfs.quota.limit-set $B0/${V0}1/a)
quota_size_val1=$(get_hex_xattr trusted.glusterfs.quota.size $B0/${V0}1/a)

#Trigger entry,metadata self-heal
TEST ls $M0/a

quota_limit_val0=$(get_hex_xattr trusted.glusterfs.quota.limit-set $B0/${V0}0/a)
quota_size_val0=$(get_hex_xattr trusted.glusterfs.quota.size $B0/${V0}0/a)

#Test that limit-set xattr is healed
TEST [ $quota_limit_val0 == $quota_limit_val1 ]

#Only entry, metadata self-heal is done quota size value should not be same
TEST [ $quota_size_val0 != $quota_size_val1 ]
TEST cat $M0/a/f

#Now that data self-heal is done quota size value should be same
quota_size_val0=$(get_hex_xattr trusted.glusterfs.quota.size $B0/${V0}0/a)
TEST [ $quota_size_val0 == $quota_size_val1 ]
cleanup
