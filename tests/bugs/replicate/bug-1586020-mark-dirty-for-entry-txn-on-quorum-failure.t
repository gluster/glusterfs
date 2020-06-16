#!/bin/bash

. $(dirname $0)/../../include.rc
. $(dirname $0)/../../volume.rc

cleanup;

function create_files {
        local i=1
        while (true)
        do
                dd if=/dev/zero of=$M0/file$i bs=1M count=10
                if [ -e $B0/${V0}0/file$i ] || [ -e $B0/${V0}1/file$i ]; then
                        ((i++))
                else
                        break
                fi
        done
        echo $i
}

TEST glusterd

#Create brick partitions
TEST truncate -s 100M $B0/brick0
TEST truncate -s 100M $B0/brick1
#Have the 3rd brick of a higher size to test the scenario of entry transaction
#passing on only one brick and not on other bricks.
TEST truncate -s 110M $B0/brick2
LO1=`SETUP_LOOP $B0/brick0`
TEST [ $? -eq 0 ]
TEST MKFS_LOOP $LO1
LO2=`SETUP_LOOP $B0/brick1`
TEST [ $? -eq 0 ]
TEST MKFS_LOOP $LO2
LO3=`SETUP_LOOP $B0/brick2`
TEST [ $? -eq 0 ]
TEST MKFS_LOOP $LO3
TEST mkdir -p $B0/${V0}0 $B0/${V0}1 $B0/${V0}2
TEST MOUNT_LOOP $LO1 $B0/${V0}0
TEST MOUNT_LOOP $LO2 $B0/${V0}1
TEST MOUNT_LOOP $LO3 $B0/${V0}2

TEST $CLI volume create $V0 replica 3 $H0:$B0/${V0}{0,1,2}
TEST $CLI volume start $V0
TEST $CLI volume set $V0 performance.write-behind off
TEST $CLI volume set $V0 self-heal-daemon off
TEST $GFS --volfile-server=$H0 --volfile-id=$V0 $M0

i=$(create_files)
TEST ! ls $B0/${V0}0/file$i
TEST ! ls $B0/${V0}1/file$i
TEST ls $B0/${V0}2/file$i
dirty=$(get_hex_xattr trusted.afr.dirty $B0/${V0}2)
TEST [ "$dirty" != "000000000000000000000000" ]

TEST $CLI volume set $V0 self-heal-daemon on
EXPECT_WITHIN $PROCESS_UP_TIMEOUT "Y" glustershd_up_status
EXPECT_WITHIN $CHILD_UP_TIMEOUT "1" afr_child_up_status_in_shd $V0 0
EXPECT_WITHIN $CHILD_UP_TIMEOUT "1" afr_child_up_status_in_shd $V0 1
EXPECT_WITHIN $CHILD_UP_TIMEOUT "1" afr_child_up_status_in_shd $V0 2
TEST rm -f $M0/file1

TEST $CLI volume heal $V0
EXPECT_WITHIN $HEAL_TIMEOUT "0" get_pending_heal_count $V0
TEST force_umount $M0
TEST $CLI volume stop $V0
EXPECT 'Stopped' volinfo_field $V0 'Status';
TEST $CLI volume delete $V0;
UMOUNT_LOOP ${B0}/${V0}{0,1,2}
rm -f ${B0}/brick{0,1,2}
cleanup;
