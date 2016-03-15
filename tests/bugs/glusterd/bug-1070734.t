#!/bin/bash

. $(dirname $0)/../../include.rc
. $(dirname $0)/../../volume.rc
. $(dirname $0)/../../nfs.rc

cleanup;

## Start glusterd
TEST glusterd;
TEST pidof glusterd;
TEST $CLI volume info;

## Lets create volume
TEST $CLI volume create $V0 $H0:$B0/${V0}{1,2};

## Verify volume is created
EXPECT "$V0" volinfo_field $V0 'Volume Name';
EXPECT 'Created' volinfo_field $V0 'Status';
TEST $CLI volume set $V0 nfs.disable false

## Start volume and verify
TEST $CLI volume start $V0;
EXPECT 'Started' volinfo_field $V0 'Status';

EXPECT_WITHIN $NFS_EXPORT_TIMEOUT "1" is_nfs_export_available;
TEST mount_nfs $H0:/$V0 $N0;

############################################################################
#TEST-PLAN:
#Create a directory DIR and a file inside DIR
#check the hash brick of the file
#delete the directory for recreating later after remove-brick
#remove the brick where the files hashed to
#After remove-brick status says complete go on creating the same directory \
#DIR and file
#Check if the file now falls into the other brick
#Check if the other brick gets the full layout and the remove brick gets \
#the zeroed layout
############################################################################

TEST mkdir $N0/DIR;

TEST touch $N0/DIR/file;

if [ -f $B0/${V0}1/DIR/file ]
then
        HASHED=$B0/${V0}1;
        OTHERBRICK=$B0/${V0}2;
else
        HASHED=$B0/${V0}2;
        OTHERBRICK=$B0/${V0}1;
fi

TEST rm -f $N0/DIR/file;
TEST rmdir $N0/DIR;
TEST $CLI volume remove-brick $V0 $H0:${HASHED} start;
EXPECT_WITHIN $REBALANCE_TIMEOUT "completed" remove_brick_status_completed_field "$V0" \
"$H0:${HASHED}";

TEST mkdir $N0/DIR;
TEST touch $N0/DIR/file;

#Check now the file should fall in to OTHERBRICK
TEST [ -f ${OTHERBRICK}/DIR/file ]

#Check the DIR on HASHED should have got zeroed layout and the \
#OTHERBRICK should have got full layout
shorter_layout () {
	dht_get_layout $1 | cut -c 19-
}
EXPECT "0000000000000000" shorter_layout $HASHED/DIR ;
EXPECT "00000000ffffffff" shorter_layout $OTHERBRICK/DIR;

## Before killing daemon to avoid deadlocks
EXPECT_WITHIN $UMOUNT_TIMEOUT "Y" umount_nfs $N0

cleanup
