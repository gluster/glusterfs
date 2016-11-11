#!/bin/bash

. $(dirname $0)/../../include.rc
. $(dirname $0)/../../volume.rc

cleanup;

## 1. Start glusterd
TEST glusterd;

## 2. Lets create volume
TEST $CLI volume create $V0 $H0:$B0/${V0}{1,2,3};

## 3. Enable the upcall xlator, and increase the md-cache timeout to max
TEST $CLI volume set $V0 features.cache-invalidation on
TEST $CLI volume set $V0 features.cache-invalidation-timeout 600
TEST $CLI volume set $V0 indexing on

## 6. Start the volume
TEST $CLI volume start $V0

## 7. Create two gluster mounts
TEST glusterfs --volfile-id=/$V0 --volfile-server=$H0 $M0

## 8. Create directory and files from the M0
TEST touch $M0/file1
TEST mv $M0/file1 $M0/file2

cleanup;
