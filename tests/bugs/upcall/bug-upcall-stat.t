#!/bin/bash

. $(dirname $0)/../../include.rc
. $(dirname $0)/../../volume.rc

cleanup;
TEST glusterd;

TEST $CLI volume create $V0 $H0:$B0/${V0}{1};

TEST $CLI volume set $V0 features.cache-invalidation on
TEST $CLI volume set $V0 features.cache-invalidation-timeout 600
TEST $CLI volume set $V0 performance.cache-invalidation on
TEST $CLI volume set $V0 performance.md-cache-timeout 600
TEST $CLI volume set $V0 performance.cache-samba-metadata on

#TEST $CLI volume stop $V0
TEST $CLI volume start $V0

## 5. Create two gluster mounts
TEST glusterfs --volfile-id=/$V0 --volfile-server=$H0 $M0
TEST glusterfs --volfile-id=/$V0 --volfile-server=$H0 $M1

TEST $CLI volume profile $V0 start

## 8. Create a file
TEST touch $M0/file1

TEST "setfattr -n user.DOSATTRIB -v "abc" $M0/file1"
TEST "getfattr -n user.DOSATTRIB $M1/file1 | grep -q abc"
TEST "setfattr -n user.DOSATTRIB -v "xyz" $M0/file1"
sleep 2; #There can be a very very small window where the next getxattr
         #reaches md-cache, before the cache-invalidation caused by previous
         #setxattr, reaches md-cache. Hence sleeping for 2 sec.
         #Also it should not be > 600.
TEST "getfattr -n user.DOSATTRIB $M1/file1 | grep -q xyz"

$CLI volume profile $V0 info | grep -q CI_XATTR
EXPECT '0' echo $?
