#!/bin/bash

. $(dirname $0)/../../include.rc
. $(dirname $0)/../../volume.rc
. $(dirname $0)/../../snapshot.rc

cleanup;
TEST verify_lvm_version;
TEST glusterd;
TEST pidof glusterd;

TEST setup_lvm 1

TEST $CLI volume create $V0 $H0:$L1
TEST $CLI volume start $V0

TEST $CLI snapshot config activate-on-create enable
TEST $CLI volume set $V0 features.uss enable

TEST glusterfs -s $H0 --volfile-id=$V0 $M0

TEST mkdir $M0/xyz

TEST $CLI snapshot create snap1 $V0 no-timestamp
TEST $CLI snapshot create snap2 $V0 no-timestamp

TEST rmdir $M0/xyz

TEST $CLI snapshot create snap3 $V0 no-timestamp
TEST $CLI snapshot create snap4 $V0 no-timestamp

TEST mkdir $M0/xyz
TEST ls $M0/xyz/.snaps/

TEST $CLI volume stop $V0
TEST $CLI snapshot restore snap2
TEST $CLI volume start $V0

EXPECT_WITHIN $UMOUNT_TIMEOUT "Y" force_umount $M0
TEST glusterfs -s $H0 --volfile-id=$V0 $M0

#Dir xyz exists in snap1
EXPECT_WITHIN $PROCESS_UP_TIMEOUT "0" STAT $M0/xyz

EXPECT_WITHIN $PROCESS_UP_TIMEOUT "3" count_snaps $M0/xyz
TEST mkdir $M0/abc
EXPECT_WITHIN $PROCESS_UP_TIMEOUT "3" count_snaps $M0/abc

#Clean up
TEST $CLI snapshot delete snap1
TEST $CLI snapshot delete snap3
TEST $CLI snapshot delete snap4
TEST $CLI volume stop $V0 force
TEST $CLI volume delete $V0

cleanup;
