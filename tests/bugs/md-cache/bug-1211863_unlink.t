#!/bin/bash

. $(dirname $0)/../../include.rc
. $(dirname $0)/../../volume.rc

cleanup;

## Start glusterd
TEST glusterd;

## Lets create volume
TEST $CLI volume create $V0 $H0:$B0/${V0}{1,2,3};

## Start the volume
TEST $CLI volume start $V0

## Enable the upcall xlator, and increase the md-cache timeout to max
TEST $CLI volume set $V0 features.cache-invalidation on
TEST $CLI volume set $V0 features.cache-invalidation-timeout 600
TEST $CLI volume set $V0 performance.cache-invalidation on
TEST $CLI volume set $V0 performance.md-cache-timeout 600
TEST $CLI volume set $V0 performance.cache-samba-metadata on

## Create two gluster mounts
TEST glusterfs --volfile-id=/$V0 --volfile-server=$H0 $M0
TEST glusterfs --volfile-id=/$V0 --volfile-server=$H0 $M1

## Create files and directories from M0
TEST mkdir $M0/dir1
TEST touch $M0/dir1/file{1..5}

## Lookup few files from M1, so that md-cache cahces
TEST ls -l $M1/dir1/file2
TEST ls -l $M1/dir1/file3

## Remove the looked up file from M0
TEST rm $M0/dir1/file2
TEST mv $M0/dir1/file3 $M0/dir1/file6

## Check if the files are not visible from both M0 and M1
EXPECT_WITHIN $MDC_TIMEOUT "N" path_exists $M0/dir1/file2
EXPECT_WITHIN $MDC_TIMEOUT "N" path_exists $M0/dir1/file3
EXPECT_WITHIN $MDC_TIMEOUT "Y" path_exists $M0/dir1/file6

EXPECT_WITHIN $MDC_TIMEOUT "N" path_exists $M1/dir1/file2
EXPECT_WITHIN $MDC_TIMEOUT "N" path_exists $M1/dir1/file3
EXPECT_WITHIN $MDC_TIMEOUT "Y" path_exists $M1/dir1/file6

cleanup;
