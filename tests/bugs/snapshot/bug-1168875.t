#!/bin/bash

. $(dirname $0)/../../include.rc
. $(dirname $0)/../../volume.rc
. $(dirname $0)/../../snapshot.rc
. $(dirname $0)/../../fileio.rc
. $(dirname $0)/../../nfs.rc

cleanup;

function check_entry_point_exists ()
{
        local entry_point=$1;
        local _path=$2;

        ls -a $_path | grep $entry_point;

        if [ $? -eq 0 ]; then
            echo 'Y';
        else
            echo 'N';
        fi
}

TEST init_n_bricks 3;
TEST setup_lvm 3;

TEST glusterd;

TEST pidof glusterd;

TEST $CLI volume create $V0 $H0:$L1 $H0:$L2 $H0:$L3;

TEST $CLI volume start $V0;

TEST glusterfs --volfile-server=$H0 --volfile-id=$V0 --xlator-option *-snapview-client.snapdir-entry-path=/dir $M0;

TEST glusterfs --volfile-server=$H0 --volfile-id=$V0 $N0;
for i in {1..10} ; do echo "file" > $M0/file$i ; done


for i in {11..20} ; do echo "file" > $M0/file$i ; done

mkdir $M0/dir;

for i in {1..10} ; do echo "file" > $M0/dir/file$i ; done

mkdir $M0/dir1;
mkdir $M0/dir2;

for i in {1..10} ; do echo "foo" > $M0/dir1/foo$i ; done
for i in {1..10} ; do echo "foo" > $M0/dir2/foo$i ; done

for i in {11..20} ; do echo "foo" > $M0/dir1/foo$i ; done
for i in {11..20} ; do echo "foo" > $M0/dir2/foo$i ; done

TEST $CLI snapshot create snap1 $V0 no-timestamp;
TEST $CLI snapshot activate snap1;

TEST $CLI volume set $V0 features.uss enable;

EXPECT_WITHIN $PROCESS_UP_TIMEOUT 'Y' check_if_snapd_exist

EXPECT_WITHIN $PROCESS_UP_TIMEOUT 'N' check_entry_point_exists .snaps $M0/dir
EXPECT_WITHIN $PROCESS_UP_TIMEOUT 'N' check_entry_point_exists .snaps $N0/dir

EXPECT_WITHIN $PROCESS_UP_TIMEOUT 'N' check_entry_point_exists .snaps $M0/dir1
EXPECT_WITHIN $PROCESS_UP_TIMEOUT 'N' check_entry_point_exists .snaps $N0/dir1

TEST $CLI volume set $V0 features.show-snapshot-directory enable;

EXPECT_WITHIN $PROCESS_UP_TIMEOUT "0" STAT $M0/dir
EXPECT_WITHIN $PROCESS_UP_TIMEOUT "0" STAT $N0/dir
EXPECT_WITHIN $PROCESS_UP_TIMEOUT "0" STAT $M0/dir1
EXPECT_WITHIN $PROCESS_UP_TIMEOUT "0" STAT $N0/dir1

EXPECT_WITHIN $PROCESS_UP_TIMEOUT 'Y' check_entry_point_exists ".snaps" $M0/dir
EXPECT_WITHIN $PROCESS_UP_TIMEOUT 'N' check_entry_point_exists ".snaps" $N0/dir

EXPECT_WITHIN $PROCESS_UP_TIMEOUT 'N' check_entry_point_exists ".snaps" $M0/dir1
EXPECT_WITHIN $PROCESS_UP_TIMEOUT 'N' check_entry_point_exists ".snaps" $N0/dir1

TEST $CLI volume set $V0 features.show-snapshot-directory disable;

EXPECT_WITHIN $PROCESS_UP_TIMEOUT "0" STAT $M0/dir
EXPECT_WITHIN $PROCESS_UP_TIMEOUT "0" STAT $N0/dir
EXPECT_WITHIN $PROCESS_UP_TIMEOUT "0" STAT $M0/dir1
EXPECT_WITHIN $PROCESS_UP_TIMEOUT "0" STAT $N0/dir1

EXPECT_WITHIN $PROCESS_UP_TIMEOUT 'N' check_entry_point_exists ".snaps" $M0/dir
EXPECT_WITHIN $PROCESS_UP_TIMEOUT 'N' check_entry_point_exists ".snaps" $N0/dir

EXPECT_WITHIN $PROCESS_UP_TIMEOUT 'N' check_entry_point_exists ".snaps" $M0/dir1
EXPECT_WITHIN $PROCESS_UP_TIMEOUT 'N' check_entry_point_exists ".snaps" $N0/dir1

cleanup;
