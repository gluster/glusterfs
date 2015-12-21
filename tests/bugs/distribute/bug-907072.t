#!/bin/bash

. $(dirname $0)/../../include.rc
. $(dirname $0)/../../fileio.rc
. $(dirname $0)/../../dht.rc
. $(dirname $0)/../../volume.rc

cleanup;

TEST glusterd;
TEST pidof glusterd;

TEST $CLI volume create $V0 $H0:$B0/${V0}{0,1,2,3};
TEST $CLI volume start $V0;

TEST glusterfs -s $H0 --volfile-id $V0 $M0;

TEST mkdir $M0/test;

# Extract the layout sans the commit hash
OLD_LAYOUT0=`get_layout $B0/${V0}0/test | cut -c11-34`;
OLD_LAYOUT1=`get_layout $B0/${V0}1/test | cut -c11-34`;
OLD_LAYOUT2=`get_layout $B0/${V0}2/test | cut -c11-34`;
OLD_LAYOUT3=`get_layout $B0/${V0}3/test | cut -c11-34`;

TEST killall glusterfsd;

# Delete directory on one brick
TEST rm -rf $B0/${V}1/test;

# And only layout xattr on another brick
TEST setfattr -x trusted.glusterfs.dht $B0/${V0}2/test;

TEST $CLI volume start $V0 force;

EXPECT_WITHIN $UMOUNT_TIMEOUT "Y" force_umount $M0
TEST glusterfs -s $H0 --volfile-id $V0 $M0;
TEST stat $M0/test;

# Extract the layout sans the commit hash
NEW_LAYOUT0=`get_layout $B0/${V0}0/test | cut -c11-34`;
NEW_LAYOUT1=`get_layout $B0/${V0}1/test | cut -c11-34`;
NEW_LAYOUT2=`get_layout $B0/${V0}2/test | cut -c11-34`;
NEW_LAYOUT3=`get_layout $B0/${V0}3/test | cut -c11-34`;

EXPECT $OLD_LAYOUT0 echo $NEW_LAYOUT0;
EXPECT $OLD_LAYOUT1 echo $NEW_LAYOUT1;
EXPECT $OLD_LAYOUT2 echo $NEW_LAYOUT2;
EXPECT $OLD_LAYOUT3 echo $NEW_LAYOUT3;
