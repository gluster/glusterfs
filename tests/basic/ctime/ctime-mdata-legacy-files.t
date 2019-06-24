#!/bin/bash
. $(dirname $0)/../../include.rc
. $(dirname $0)/../../volume.rc
. $(dirname $0)/../../afr.rc
cleanup;

###############################################################################
#Replica volume

TEST glusterd
TEST pidof glusterd
TEST $CLI volume create $V0 replica 3 $H0:$B0/${V0}{0,1,2}
TEST $CLI volume set $V0 performance.stat-prefetch off
TEST $CLI volume start $V0

TEST glusterfs --volfile-id=$V0 --volfile-server=$H0 --entry-timeout=0 $M0;

#Disable ctime and create file, file doesn't contain "trusted.glusterfs.mdata" xattr
TEST $CLI volume set $V0 ctime off

TEST "mkdir $M0/DIR"
TEST "echo hello_world > $M0/DIR/FILE"

#Verify absence of xattr
EXPECT_WITHIN $PROCESS_UP_TIMEOUT "" check_for_xattr 'trusted.glusterfs.mdata' "$B0/${V0}0/DIR"
EXPECT_WITHIN $PROCESS_UP_TIMEOUT "" check_for_xattr 'trusted.glusterfs.mdata' "$B0/${V0}0/DIR/FILE"
EXPECT_WITHIN $PROCESS_UP_TIMEOUT "" check_for_xattr 'trusted.glusterfs.mdata' "$B0/${V0}1/DIR"
EXPECT_WITHIN $PROCESS_UP_TIMEOUT "" check_for_xattr 'trusted.glusterfs.mdata' "$B0/${V0}1/DIR/FILE"
EXPECT_WITHIN $PROCESS_UP_TIMEOUT "" check_for_xattr 'trusted.glusterfs.mdata' "$B0/${V0}2/DIR"
EXPECT_WITHIN $PROCESS_UP_TIMEOUT "" check_for_xattr 'trusted.glusterfs.mdata' "$B0/${V0}2/DIR/FILE"

#Enable ctime
TEST $CLI volume set $V0 ctime on
sleep 3
TEST stat $M0/DIR/FILE

#Verify presence "trusted.glusterfs.mdata" xattr on backend
#The lookup above should have created xattr
EXPECT_WITHIN $PROCESS_UP_TIMEOUT 'trusted.glusterfs.mdata' check_for_xattr 'trusted.glusterfs.mdata' "$B0/${V0}0/DIR"
EXPECT_WITHIN $PROCESS_UP_TIMEOUT 'trusted.glusterfs.mdata' check_for_xattr 'trusted.glusterfs.mdata' "$B0/${V0}0/DIR/FILE"
EXPECT_WITHIN $PROCESS_UP_TIMEOUT 'trusted.glusterfs.mdata' check_for_xattr 'trusted.glusterfs.mdata' "$B0/${V0}1/DIR"
EXPECT_WITHIN $PROCESS_UP_TIMEOUT 'trusted.glusterfs.mdata' check_for_xattr 'trusted.glusterfs.mdata' "$B0/${V0}1/DIR/FILE"
EXPECT_WITHIN $PROCESS_UP_TIMEOUT 'trusted.glusterfs.mdata' check_for_xattr 'trusted.glusterfs.mdata' "$B0/${V0}2/DIR"
EXPECT_WITHIN $PROCESS_UP_TIMEOUT 'trusted.glusterfs.mdata' check_for_xattr 'trusted.glusterfs.mdata' "$B0/${V0}2/DIR/FILE"

###############################################################################
#Disperse Volume

TEST $CLI volume create $V1 disperse 3 redundancy 1  $H0:$B0/${V1}{0,1,2}
TEST $CLI volume set $V1 performance.stat-prefetch off
TEST $CLI volume start $V1

TEST glusterfs --volfile-id=$V1 --volfile-server=$H0 --entry-timeout=0 $M1;

#Disable ctime and create file, file doesn't contain "trusted.glusterfs.mdata" xattr
TEST $CLI volume set $V1 ctime off
TEST "mkdir $M1/DIR"
TEST "echo hello_world > $M1/DIR/FILE"

#Verify absence of xattr
EXPECT_WITHIN $PROCESS_UP_TIMEOUT "" check_for_xattr 'trusted.glusterfs.mdata' "$B0/${V1}0/DIR"
EXPECT_WITHIN $PROCESS_UP_TIMEOUT "" check_for_xattr 'trusted.glusterfs.mdata' "$B0/${V1}0/DIR/FILE"
EXPECT_WITHIN $PROCESS_UP_TIMEOUT "" check_for_xattr 'trusted.glusterfs.mdata' "$B0/${V1}1/DIR"
EXPECT_WITHIN $PROCESS_UP_TIMEOUT "" check_for_xattr 'trusted.glusterfs.mdata' "$B0/${V1}1/DIR/FILE"
EXPECT_WITHIN $PROCESS_UP_TIMEOUT "" check_for_xattr 'trusted.glusterfs.mdata' "$B0/${V1}2/DIR"
EXPECT_WITHIN $PROCESS_UP_TIMEOUT "" check_for_xattr 'trusted.glusterfs.mdata' "$B0/${V1}2/DIR/FILE"

#Enable ctime
TEST $CLI volume set $V1 ctime on
sleep 3
TEST stat $M1/DIR/FILE

#Verify presence "trusted.glusterfs.mdata" xattr on backend
#The lookup above should have created xattr
EXPECT_WITHIN $PROCESS_UP_TIMEOUT 'trusted.glusterfs.mdata' check_for_xattr 'trusted.glusterfs.mdata' "$B0/${V1}0/DIR"
EXPECT_WITHIN $PROCESS_UP_TIMEOUT 'trusted.glusterfs.mdata' check_for_xattr 'trusted.glusterfs.mdata' "$B0/${V1}0/DIR/FILE"
EXPECT_WITHIN $PROCESS_UP_TIMEOUT 'trusted.glusterfs.mdata' check_for_xattr 'trusted.glusterfs.mdata' "$B0/${V1}1/DIR"
EXPECT_WITHIN $PROCESS_UP_TIMEOUT 'trusted.glusterfs.mdata' check_for_xattr 'trusted.glusterfs.mdata' "$B0/${V1}1/DIR/FILE"
EXPECT_WITHIN $PROCESS_UP_TIMEOUT 'trusted.glusterfs.mdata' check_for_xattr 'trusted.glusterfs.mdata' "$B0/${V1}2/DIR"
EXPECT_WITHIN $PROCESS_UP_TIMEOUT 'trusted.glusterfs.mdata' check_for_xattr 'trusted.glusterfs.mdata' "$B0/${V1}2/DIR/FILE"

cleanup;
###############################################################################
