#!/bin/bash

. $(dirname $0)/../include.rc
. $(dirname $0)/../volume.rc

cleanup;

TEST glusterd
TEST pidof glusterd
TEST $CLI volume info;

TEST $CLI volume create $V0 $H0:$B0/brick1 $H0:$B0/brick2;
EXPECT 'Created' volinfo_field $V0 'Status';

TEST $CLI volume start $V0;
EXPECT 'Started' volinfo_field $V0 'Status';

MOUNTDIR=$M0;
TEST glusterfs --mem-accounting --volfile-server=$H0 --volfile-id=$V0 $MOUNTDIR;

sdump1=$(generate_mount_statedump $V0);
nalloc1=0
grep -A2 "fuse - usage-type 85" $sdump1
if [ $? -eq '0' ]
then
        nalloc1=`grep -A2 "fuse - usage-type 85" $sdump1 | grep num_allocs | cut -d '=' -f2`
fi

build_tester $(dirname $0)/bug-834465.c

TEST $(dirname $0)/bug-834465 $M0/testfile

sdump2=$(generate_mount_statedump $V0);
nalloc2=`grep -A2 "fuse - usage-type 85" $sdump2 | grep num_allocs | cut -d '=' -f2`

TEST [ $nalloc1 -eq $nalloc2 ];

TEST rm -rf $MOUNTDIR/*
TEST rm -rf $(dirname $0)/bug-834465
cleanup_mount_statedump $V0

TEST   umount $MOUNTDIR -l

cleanup;
