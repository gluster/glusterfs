#!/bin/bash

. $(dirname $0)/../../include.rc
. $(dirname $0)/../../volume.rc

cleanup;

## 1. Start glusterd
TEST glusterd;

## 2. Lets create volume
TEST $CLI volume create $V0 $H0:$B0/${V0}{1,2,3};

## 3. Start the volume
TEST $CLI volume start $V0

## 4. Enable the upcall xlator, and increase the md-cache timeout to max
TEST $CLI volume set $V0 performance.md-cache-timeout 600
TEST $CLI volume set $V0 performance.cache-samba-metadata on

## 6. Create two gluster mounts
TEST glusterfs --volfile-id=/$V0 --volfile-server=$H0 $M0
TEST glusterfs --volfile-id=/$V0 --volfile-server=$H0 $M1

## 8. Create a file
TEST touch $M0/file1

## 9. Setxattr from mount-0
TEST "setfattr -n user.DOSATTRIB -v "abc" $M0/file1"
## 10. Getxattr from mount-1, this should return the correct value as it is a fresh getxattr
TEST "getfattr -n user.DOSATTRIB $M1/file1 | grep -q abc"

## 11. Now modify the same xattr from mount-0 again
TEST "setfattr -n user.DOSATTRIB -v "xyz" $M0/file1"
## 12. Since the xattr is already cached in mount-1 it returns the old xattr
       #value, until the timeout (600)
TEST "getfattr -n user.DOSATTRIB $M1/file1 | grep -q abc"

## 13. Unmount to clean all the cache
EXPECT_WITHIN $UMOUNT_TIMEOUT "Y" force_umount $M0
EXPECT_WITHIN $UMOUNT_TIMEOUT "Y" force_umount $M1

TEST $CLI volume set $V0 features.cache-invalidation on
TEST $CLI volume set $V0 features.cache-invalidation-timeout 600
TEST $CLI volume set $V0 performance.cache-invalidation on

## 19. Restart the volume to restart the bick process
TEST $CLI volume stop $V0
TEST $CLI volume start $V0

## 21. Create two gluster mounts
TEST glusterfs --volfile-id=/$V0 --volfile-server=$H0 $M0
TEST glusterfs --volfile-id=/$V0 --volfile-server=$H0 $M1

## 23. Repeat the tests 11-14, but this time since cache invalidation is on,
       #the getxattr will reflect the new value
TEST "setfattr -n user.DOSATTRIB -v "abc" $M0/file1"
TEST "getfattr -n user.DOSATTRIB $M1/file1 | grep -q abc"
TEST "setfattr -n user.DOSATTRIB -v "xyz" $M0/file1"
sleep 2; #There can be a very very small window where the next getxattr
         #reaches md-cache, before the cache-invalidation caused by previous
         #setxattr, reaches md-cache. Hence sleeping for 2 sec.
         #Also it should not be > 600.
TEST "getfattr -n user.DOSATTRIB $M1/file1 | grep -q xyz"

TEST $CLI volume set $V0 cache-samba-metadata off
EXPECT 'off' volinfo_field $V0 'performance.cache-samba-metadata'

cleanup;
