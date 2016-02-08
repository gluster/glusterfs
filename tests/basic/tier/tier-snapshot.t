#!/bin/bash
. $(dirname $0)/../../include.rc
. $(dirname $0)/../../volume.rc
. $(dirname $0)/../../snapshot.rc

cleanup;

TEST init_n_bricks 4;
TEST setup_lvm 4;

TEST glusterd;

TEST pidof glusterd;

TEST $CLI volume create $V0 replica 2 $H0:$L1 $H0:$L2 ;

TEST $CLI volume start $V0;

TEST $CLI volume attach-tier $V0 replica 2 $H0:$L3 $H0:$L4 ;

TEST $GFS --volfile-server=$H0 --volfile-id=$V0 $M0;

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

TEST $CLI snapshot delete all;

cleanup;
#G_TESTDEF_TEST_STATUS_NETBSD7=BAD_TEST,BUG=000000
#G_TESTDEF_TEST_STATUS_CENTOS6=BAD_TEST,BUG=000000
