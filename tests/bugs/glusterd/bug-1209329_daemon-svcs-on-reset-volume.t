#!/bin/bash

. $(dirname $0)/../../include.rc
. $(dirname $0)/../../volume.rc
. $(dirname $0)/../../nfs.rc

cleanup;

## Start glusterd
TEST glusterd;
TEST pidof glusterd;

## Lets create volume
TEST $CLI volume create $V0 $H0:$B0/${V0};

## Verify volume is created
EXPECT "$V0" volinfo_field $V0 'Volume Name';
EXPECT 'Created' volinfo_field $V0 'Status';

## Start volume and verify
TEST $CLI volume start $V0;
EXPECT 'Started' volinfo_field $V0 'Status';

##enable the bitrot and verify bitd is running or not
TEST $CLI volume bitrot $V0 enable
EXPECT 'on' volinfo_field $V0 'features.bitrot'
EXPECT_WITHIN $PROCESS_UP_TIMEOUT "1" get_bitd_count

##Do reset force which set the bitrot options to default
TEST $CLI volume reset $V0 force;
EXPECT_WITHIN $PROCESS_UP_TIMEOUT "0" get_bitd_count

##enable the uss option and verify snapd is running or not
TEST $CLI volume set $V0 features.uss on
EXPECT 'on' volinfo_field $V0 'features.uss'
EXPECT_WITHIN $PROCESS_UP_TIMEOUT "1" get_snapd_count

##Do reset force which set the uss options to default
TEST $CLI volume reset $V0 force;
EXPECT_WITHIN $PROCESS_UP_TIMEOUT "0" get_snapd_count

##verify initial nfs disabled by default
EXPECT "0" get_nfs_count

##enable nfs and verify
TEST $CLI volume set $V0 nfs.disable off
EXPECT_WITHIN $NFS_EXPORT_TIMEOUT "1" is_nfs_export_available
EXPECT "1" get_nfs_count

##Do reset force which set the nfs.option to default
TEST $CLI volume reset $V0 force;
EXPECT_WITHIN $PROCESS_UP_TIMEOUT "0" get_nfs_count

##enable the uss option and verify snapd is running or not
TEST $CLI volume set $V0 features.uss on
EXPECT 'on' volinfo_field $V0 'features.uss'
EXPECT_WITHIN $PROCESS_UP_TIMEOUT "1" get_snapd_count

##Disable the uss option using set command and verify snapd
TEST $CLI volume set $V0 features.uss  off
EXPECT_WITHIN $PROCESS_UP_TIMEOUT "0" get_snapd_count

##enable nfs.disable and verify
TEST $CLI volume set $V0 nfs.disable on
EXPECT 'on' volinfo_field $V0 'nfs.disable'
EXPECT_WITHIN $PROCESS_UP_TIMEOUT "0" get_nfs_count

## disable nfs.disable option using set command
TEST $CLI volume set $V0 nfs.disable  off
EXPECT_WITHIN $PROCESS_UP_TIMEOUT "1" get_nfs_count

cleanup;
