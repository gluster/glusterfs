#!/bin/bash

. $(dirname $0)/../include.rc
. $(dirname $0)/../volume.rc

cleanup;

TEST glusterd

TEST $CLI volume create $V0 $H0:$B0/${V0}{0..4}
EXPECT 'Created' volinfo_field $V0 'Status'

TEST $CLI volume set $V0 group nl-cache
EXPECT '600' volinfo_field $V0 'performance.nl-cache-timeout'
EXPECT 'on' volinfo_field $V0 'performance.nl-cache'
EXPECT '600' volinfo_field $V0 'features.cache-invalidation-timeout'
EXPECT 'on' volinfo_field $V0 'features.cache-invalidation'
EXPECT '200000' volinfo_field $V0  'network.inode-lru-limit'
TEST $CLI volume set $V0 nl-cache-positive-entry on

TEST $CLI volume start $V0;
EXPECT 'Started' volinfo_field $V0 'Status';

TEST glusterfs --volfile-id=/$V0 --volfile-server=$H0 $M0
TEST glusterfs --volfile-id=/$V0 --volfile-server=$H0 $M1

TEST ! ls $M0/file2
TEST touch $M0/file1
TEST ! ls $M0/file2
TEST touch $M0/file2
TEST ls $M0/file2
TEST rm $M0/file2
TEST rm $M0/file1

TEST mkdir $M0/dir1
TEST ! ls -l $M0/dir1/file
TEST mkdir $M0/dir1/dir2
TEST ! ls -l $M0/dir1/file
TEST ! ls -l $M0/dir1/dir2/file
TEST ls -l $M0/dir1/dir2
TEST rmdir $M0/dir1/dir2
TEST rmdir $M0/dir1

TEST ! ls -l $M0/file2
TEST touch $M1/file2
TEST ls -l $M0/file2
TEST rm $M1/file2

TEST ! ls -l $M0/dir1
TEST mkdir $M1/dir1
TEST ls -l $M0/dir1
TEST ! ls -l $M0/dir1/file1
TEST mkdir $M1/dir1/dir2
TEST ! ls -l $M0/dir1/file1
TEST ls -l $M0/dir1/dir2
TEST ! ls -l $M1/dir1/file1

TEST touch $M0/dir1/file
TEST ln $M0/dir1/file $M0/dir1/file_link
TEST ls -l $M1/dir1/file
TEST ls -l $M1/dir1/file_link
TEST rm $M0/dir1/file
TEST rm $M0/dir1/file_link
TEST rmdir $M0/dir1/dir2
TEST rmdir $M0/dir1

cleanup;
#G_TESTDEF_TEST_STATUS_NETBSD7=BAD_TEST,BUG=000000
