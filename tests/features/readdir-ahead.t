#!/bin/bash
#
# Test basic readdir-ahead functionality. Verify that readdir-ahead can be
# enabled, create a set of files and run some ls tests.
#
###

. $(dirname $0)/../include.rc
. $(dirname $0)/../volume.rc

cleanup;

TEST glusterd

TEST $CLI volume create $V0 $H0:$B0/$V0
TEST $CLI volume start $V0

TEST $CLI volume set $V0 readdir-ahead on

TEST $GFS --volfile-id=$V0 --volfile-server=$H0 $M0

TEST mkdir $M0/test
for i in $(seq 0 99)
do
        touch $M0/test/$i
done

count=`ls -1 $M0/test | wc -l`
TEST [ $count -eq 100 ]

count=`ls -1 $M0/test | wc -l`
TEST [ $count -eq 100 ]

TEST rm -rf $M0/test/*

count=`ls -1 $M0/test | wc -l`
TEST [ $count -eq 0 ]

TEST rmdir $M0/test

EXPECT_WITHIN $UMOUNT_TIMEOUT "Y" force_umount $M0;
TEST $CLI volume stop $V0
TEST $CLI volume delete $V0

cleanup;
