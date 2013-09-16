#!/bin/bash

. $(dirname $0)/../include.rc
. $(dirname $0)/../volume.rc

cleanup;

TEST glusterd
TEST pidof glusterd
TEST $CLI volume info;

TEST $CLI volume create $V0 $H0:$B0/$V0;

EXPECT "$V0" volinfo_field $V0 'Volume Name';
EXPECT 'Created' volinfo_field $V0 'Status';

TEST $CLI volume start $V0;
EXPECT 'Started' volinfo_field $V0 'Status';

TEST $CLI volume set $V0 features.file-snapshot on;

TEST $CLI volume set $V0 performance.quick-read off;
TEST $CLI volume set $V0 performance.io-cache off;
TEST glusterfs -s $H0 --volfile-id $V0 $M0 --attribute-timeout=0;

TEST touch $M0/big-file;

TEST setfattr -n trusted.glusterfs.block-format -v qcow2:10GB $M0/big-file;

TEST ls -al $M0 # test readdirplus
TEST [ `stat -c '%s' $M0/big-file` = 10737418240 ]

echo 'ABCDEFGHIJ' > $M0/data-file1
TEST dd if=$M0/data-file1 of=$M0/big-file conv=notrunc;
TEST setfattr -n trusted.glusterfs.block-snapshot-create -v image1 $M0/big-file;

echo '1234567890' > $M0/data-file2
TEST dd if=$M0/data-file2 of=$M0/big-file conv=notrunc;
TEST setfattr -n trusted.glusterfs.block-snapshot-create -v image2 $M0/big-file;

TEST setfattr -n trusted.glusterfs.block-snapshot-goto -v image1 $M0/big-file;
TEST dd if=$M0/big-file of=$M0/out-file1 bs=11 count=1;

TEST setfattr -n trusted.glusterfs.block-snapshot-goto -v image2 $M0/big-file;
TEST dd if=$M0/big-file of=$M0/out-file2 bs=11 count=1;

TEST cmp $M0/data-file1 $M0/out-file1;
TEST cmp $M0/data-file2 $M0/out-file2;

TEST $CLI volume stop $V0;
EXPECT 'Stopped' volinfo_field $V0 'Status';

TEST $CLI volume delete $V0;
TEST ! $CLI volume info $V0;

cleanup;
