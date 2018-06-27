#!/bin/bash

. $(dirname $0)/../../include.rc
. $(dirname $0)/../../volume.rc

cleanup;

## 1. Start glusterd
TEST glusterd;

## 2. Lets create volume
TEST $CLI volume create $V0 disperse 6 redundancy 2 $H0:$B0/${V0}{0..5}

TEST $CLI volume set $V0 group metadata-cache
TEST $CLI volume set $V0 performance.xattr-cache-list "user.*"
TEST $CLI volume start $V0

TEST glusterfs --volfile-id=/$V0 --volfile-server=$H0 $M0
TEST glusterfs --volfile-id=/$V0 --volfile-server=$H0 $M1

TEST touch $M0/file1
TEST `echo "abakjshdjahskjdhakjhdskjac" >> $M0/file1`
size=`stat -c '%s' $M0/file1`

## Setxattr from mount-0
TEST "setfattr -n user.DOSATTRIB -v "abc" $M0/file1"
EXPECT $size stat -c '%s' $M0/file1

## Getxattr from mount-1, this should return the correct value
TEST "getfattr -n user.DOSATTRIB $M1/file1 | grep -q abc"

TEST "setfattr -x user.DOSATTRIB $M1/file1"
EXPECT $size stat -c '%s' $M1/file1

EXPECT_WITHIN $UMOUNT_TIMEOUT "Y" force_umount $M0
EXPECT_WITHIN $UMOUNT_TIMEOUT "Y" force_umount $M1

cleanup;
