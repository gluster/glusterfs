#!/bin/bash

. $(dirname $0)/../../include.rc
. $(dirname $0)/../../volume.rc
. $(dirname $0)/../../snapshot.rc
. $(dirname $0)/../../fileio.rc
. $(dirname $0)/../../nfs.rc

cleanup;

TEST init_n_bricks 3;
TEST setup_lvm 3;

TEST glusterd;

TEST pidof glusterd;

TEST $CLI volume create $V0 $H0:$L1 $H0:$L2 $H0:$L3;

TEST $CLI volume start $V0;

TEST glusterfs --volfile-server=$H0 --volfile-id=$V0 $M0;

for i in {1..10} ; do echo "file" > $M0/file$i ; done

TEST $CLI snapshot create snap1 $V0 no-timestamp;

for i in {11..20} ; do echo "file" > $M0/file$i ; done

TEST $CLI snapshot create snap2 $V0 no-timestamp;

mkdir $M0/dir1;
mkdir $M0/dir2;

for i in {1..10} ; do echo "foo" > $M0/dir1/foo$i ; done
for i in {1..10} ; do echo "foo" > $M0/dir2/foo$i ; done

TEST $CLI snapshot create snap3 $V0 no-timestamp;

for i in {11..20} ; do echo "foo" > $M0/dir1/foo$i ; done
for i in {11..20} ; do echo "foo" > $M0/dir2/foo$i ; done

TEST $CLI snapshot create snap4 $V0 no-timestamp;

TEST $CLI volume set $V0 features.uss enable;

EXPECT_WITHIN $PROCESS_UP_TIMEOUT 'Y' check_if_snapd_exist

TEST $CLI volume set $V0 features.uss disable;

SNAPD_PID=$(ps auxww | grep snapd | grep -v grep | awk '{print $2}');

TEST ! [ $SNAPD_PID -gt 0 ];

TEST $CLI volume set $V0 features.uss enable;

EXPECT_WITHIN $PROCESS_UP_TIMEOUT 'Y' check_if_snapd_exist

TEST $CLI volume stop $V0;

SNAPD_PID=$(ps auxww | grep snapd | grep -v grep | awk '{print $2}');

TEST ! [ $SNAPD_PID -gt 0 ];

cleanup  ;
