#!/bin/bash

. $(dirname $0)/../../include.rc
. $(dirname $0)/../../volume.rc

cleanup;

TEST glusterd
TEST pidof glusterd
TEST $CLI volume info;
TEST $CLI volume create $V0 replica 2 $H0:$B0/brick1 $H0:$B0/brick2;
EXPECT 'Created' volinfo_field $V0 'Status';

TEST $CLI volume start $V0;
EXPECT 'Started' volinfo_field $V0 'Status';

MOUNTDIR=$M0;
TEST glusterfs --entry-timeout=0 --attribute-timeout=0 --volfile-server=$H0 --volfile-id=$V0 $MOUNTDIR;

build_tester $(dirname $0)/bug-808400-flock.c
build_tester $(dirname $0)/bug-808400-fcntl.c

TEST $(dirname $0)/bug-808400-flock $MOUNTDIR/testfile \'gluster volume set $V0 performance.write-behind off\'
TEST $(dirname $0)/bug-808400-fcntl $MOUNTDIR/testfile \'gluster volume set $V0 performance.write-behind on\'

TEST rm -rf $MOUNTDIR/*
TEST rm -rf $(dirname $0)/bug-808400-flock $(dirname $0)/bug-808400-fcntl $(dirname $0)/glusterfs.log

EXPECT_WITHIN $UMOUNT_TIMEOUT "Y" force_umount $MOUNTDIR

cleanup;
