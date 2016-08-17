#!/bin/bash

. $(dirname $0)/../../include.rc
. $(dirname $0)/../../volume.rc

cleanup;

function get_permission {
        stat -c "%A" $1
}

## Start glusterd
TEST glusterd;
TEST pidof glusterd;
TEST $CLI volume info;

## Lets create volume
TEST $CLI volume create $V0 $H0:/${V0}{1,2};

## Verify volume is created
EXPECT "$V0" volinfo_field $V0 'Volume Name';
EXPECT 'Created' volinfo_field $V0 'Status';
## Start volume and verify
TEST $CLI volume start $V0;
TEST $CLI volume set $V0 performance.stat-prefetch off
EXPECT 'Started' volinfo_field $V0 'Status';
TEST glusterfs -s $H0 --volfile-id=$V0 $M0

##Test case: Add-brick
#------------------------------------------------------------
#change permission of both root
TEST chmod 444 $M0

#store permission for comparision
TEST permission_root=`stat -c "%A" $M0`
TEST echo $permission_root
#Add-brick
TEST $CLI volume add-brick $V0 $H0:/${V0}3

#Allow one lookup to happen
TEST pushd $M0
TEST ls
#Generate another lookup
echo 3 > /proc/sys/vm/drop_caches
TEST ls
#check root permission
EXPECT_WITHIN "5" $permission_root get_permission $M0
#check permission on the new-brick
EXPECT $permission_root get_permission /${V0}3
cleanup
