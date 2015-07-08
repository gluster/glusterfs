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

MOUNT_PID=`ps ax |grep "glusterfs --volfile-sever $H0 --volfile-id=$V0 $M0" | grep -v grep | awk '{print $1}' | head -1`

for i in {1..10} ; do echo "file" > $M0/file$i ; done

TEST $CLI snapshot config activate-on-create enable

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

#let snapd get started properly and client connect to snapd
EXPECT_WITHIN $CHILD_UP_TIMEOUT "1" snap_client_connected_status $V0

SNAPD_PID=$(ps auxww | grep snapd | grep -v grep | awk '{print $2}');

TEST [ $SNAPD_PID -gt 0 ];

TEST stat $M0/.snaps;

kill -KILL $SNAPD_PID;

# let snapd die properly
EXPECT_WITHIN $CHILD_UP_TIMEOUT "0" snap_client_connected_status $V0

TEST ! stat $M0/.snaps;

TEST $CLI volume start $V0 force;

# let client get the snapd port from glusterd and connect
EXPECT_WITHIN $CHILD_UP_TIMEOUT "1" snap_client_connected_status $V0

EXPECT_WITHIN $PROCESS_UP_TIMEOUT "0" STAT $M0/.snaps

cleanup;
