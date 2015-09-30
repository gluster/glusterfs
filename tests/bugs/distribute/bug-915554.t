#!/bin/bash
#
# Bug <915554>
#
# This test checks for a condition where a rebalance migrates a file and does
# not preserve the original file size. This can occur due to hole preservation
# logic in the file migration code. If a file size is aligned to a disk sector
# boundary (512b) and the tail portion of the file is zero-filled, the file
# may end up truncated to the end of the last data region in the file.
#
###

. $(dirname $0)/../../include.rc
. $(dirname $0)/../../dht.rc
. $(dirname $0)/../../volume.rc

cleanup;

TEST glusterd
TEST pidof glusterd

BRICK_COUNT=3
# create, start and mount a two brick DHT volume
TEST $CLI volume create $V0 $H0:$B0/${V0}0 $H0:$B0/${V0}1 $H0:$B0/${V0}2
TEST $CLI volume start $V0

TEST glusterfs --attribute-timeout=0 --entry-timeout=0 --gid-timeout=-1 -s $H0 --volfile-id $V0 $M0;

i=1
# Write some data to a file and extend such that the file is sparse to a sector
# aligned boundary.
echo test > $M0/$i
TEST truncate -s 1M $M0/$i

# cache the original size
SIZE1=`stat -c %s $M0/$i`

# rename till file gets a linkfile

while [ $i -ne 0 ]
do
        test=`mv $M0/$i $M0/$(( $i+1 )) 2>/dev/null`
        if [ $? -ne 0 ]
        then
                echo "rename failed"
                break
        fi
        let i++
        file_has_linkfile $i
        has_link=$?
        if [ $has_link -eq 2 ]
        then
                break;
        fi
done

# start a rebalance (force option to overide checks) to trigger migration of
# file

TEST $CLI volume rebalance $V0 start force

# check if rebalance has completed for upto 15 secs

EXPECT_WITHIN $REBALANCE_TIMEOUT "0" rebalance_completed

# validate the file size after the migration
SIZE2=`stat -c %s $M0/$i`

TEST [ $SIZE1 -eq $SIZE2 ]

TEST rm -f $M0/$i
EXPECT_WITHIN $UMOUNT_TIMEOUT "Y" force_umount $M0
TEST $CLI volume stop $V0
TEST $CLI volume delete $V0

cleanup;
