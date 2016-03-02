#!/bin/bash

. $(dirname $0)/../../include.rc
. $(dirname $0)/../../volume.rc

cleanup

TEST glusterd
TEST pidof glusterd
TEST $CLI volume create $V0 disperse 6 redundancy 2 $H0:$B0/${V0}{0..5}
TEST $CLI volume set $V0 performance.quick-read off
TEST $CLI volume set $V0 performance.io-cache off
TEST $CLI volume set $V0 performance.write-behind off
TEST $CLI volume set $V0 performance.stat-prefetch off
TEST $CLI volume set $V0 performance.read-ahead off
TEST $CLI volume heal $V0 disable
TEST $CLI volume start $V0

#Disable all caching
TEST glusterfs --direct-io-mode=yes --entry-timeout=0 --attribute-timeout=0 -s $H0 --volfile-id $V0 $M0
EXPECT_WITHIN $CHILD_UP_TIMEOUT "6" ec_child_up_count $V0 0
#TEST volume operations work fine
EXPECT "round-robin" mount_get_option_value $M0 $V0-disperse-0 read-policy
TEST $CLI volume set $V0 disperse.read-policy gfid-hash
EXPECT_WITHIN $CONFIG_UPDATE_TIMEOUT "gfid-hash" mount_get_option_value $M0 $V0-disperse-0 read-policy
TEST $CLI volume reset $V0 disperse.read-policy
EXPECT_WITHIN $CONFIG_UPDATE_TIMEOUT "round-robin" mount_get_option_value $M0 $V0-disperse-0 read-policy

#TEST if the option gives the intended behavior. The way we perform this test
#is by performing reads from the mount and write to /dev/null. If the
#read-policy is round-robin, then all bricks should have read-fop where as
#with gfid-hash number of bricks with reads should be equal to (num-bricks - redundancy)
#count

TEST $CLI volume profile $V0 start
TEST dd if=/dev/zero of=$M0/1 bs=1M count=4
#Perform reads now from file on the mount, this only tests dispatch_min
TEST dd if=$M0/1 of=/dev/null bs=1M count=4
#TEST that reads are executed on all bricks
rr_reads=$($CLI volume profile $V0 info cumulative| grep -w READ | wc -l)
EXPECT "^6$" echo $rr_reads
TEST $CLI volume profile $V0 info clear

TEST $CLI volume set $V0 disperse.read-policy gfid-hash
EXPECT_WITHIN $CONFIG_UPDATE_TIMEOUT "gfid-hash" mount_get_option_value $M0 $V0-disperse-0 read-policy

#Perform reads now from file on the mount, this only tests dispatch_min
TEST dd if=$M0/1 of=/dev/null bs=1M count=4
#TEST that reads are executed on all bricks
gh_reads=$($CLI volume profile $V0 info cumulative| grep -w READ |  wc -l)
EXPECT "^4$" echo $gh_reads

cleanup;
