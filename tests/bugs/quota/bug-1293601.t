#!/bin/bash

. $(dirname $0)/../../include.rc
. $(dirname $0)/../../volume.rc

cleanup;

TEST glusterd

TEST $CLI volume create $V0 replica 2 $H0:$B0/${V0}{1,2,3,4}
TEST $CLI volume start $V0
TEST $CLI volume quota $V0 enable

TEST glusterfs --volfile-server=$H0 --volfile-id=$V0 $M0;

for i in {1..512}; do
    dd if=/dev/zero of=$M0/f$i bs=1k count=1
done

mkdir $M0/dir1
for i in {513..1024}; do
    dd if=/dev/zero of=$M0/dir1/f$i bs=1k count=1
done

EXPECT_WITHIN $MARKER_UPDATE_TIMEOUT "1.0MB" quotausage "/"

TEST $CLI volume quota $V0 disable
TEST $CLI volume quota $V0 enable

EXPECT_WITHIN $MARKER_UPDATE_TIMEOUT "1.0MB" quotausage "/"

cleanup;
