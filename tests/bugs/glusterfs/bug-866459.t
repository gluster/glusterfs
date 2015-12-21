#!/bin/bash

. $(dirname $0)/../../include.rc
. $(dirname $0)/../../volume.rc

cleanup;


## Start and create a volume
TEST glusterd
TEST pidof glusterd
TEST $CLI volume info;

## Create and start a volume with aio enabled
TEST $CLI volume create $V0 replica 2 $H0:$B0/${V0}{1,2};
TEST $CLI volume set $V0 linux-aio on
TEST $CLI volume set $V0 background-self-heal-count 0
TEST $CLI volume set $V0 performance.stat-prefetch off;
TEST $CLI volume start $V0

## Mount FUSE with caching disabled
TEST glusterfs --entry-timeout=0 --attribute-timeout=0 -s $H0 --volfile-id $V0 $M0;

dd of=$M0/a if=/dev/urandom bs=1024k count=1 2>&1 > /dev/null
B0_hiphenated=`echo $B0 | tr '/' '-'`
## Bring a brick down
TEST kill_brick $V0 $H0 $B0/${V0}1
## Rewrite the file
dd of=$M0/a if=/dev/urandom bs=1024k count=1 2>&1 > /dev/null
TEST $CLI volume start $V0 force
## Wait for the brick to give CHILD_UP in client protocol
EXPECT_WITHIN $CHILD_UP_TIMEOUT "1" afr_child_up_status $V0 0
md5offile2=`md5sum $B0/${V0}2/a | awk '{print $1}'`

##trigger self-heal
ls -l $M0/a

EXPECT "$md5offile2" echo `md5sum $B0/${V0}1/a | awk '{print $1}'`

## Finish up
TEST $CLI volume stop $V0;
TEST $CLI volume delete $V0;

cleanup;
