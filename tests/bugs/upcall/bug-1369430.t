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
TEST $CLI volume set $V0 features.cache-invalidation on
TEST $CLI volume set $V0 features.cache-invalidation-timeout 600
TEST $CLI volume set $V0 performance.cache-invalidation on
TEST $CLI volume set $V0 performance.md-cache-timeout 600
TEST $CLI volume set $V0 performance.cache-samba-metadata on

## 8. Create two gluster mounts
TEST glusterfs --volfile-id=/$V0 --volfile-server=$H0 $M0
TEST glusterfs --volfile-id=/$V0 --volfile-server=$H0 $M1

## 10. Create directory and files from the M0
TEST mkdir $M0/dir1
TEST touch $M0/dir1/file{1..5}

## 12. Access the files via readdirp, from M1
TEST ls -l $M1/dir1/

# Change the stat of one of the files from M0 and wait for it to
# invalidate the md-cache of another mount M0
echo "hello" > $M0/dir1/file2
sleep 2;

## 13. Expct non zero size when stat from M1
EXPECT_NOT "0" stat -c %s $M1/dir1/file2

cleanup;
