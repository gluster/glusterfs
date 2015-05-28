#!/bin/bash

#Test case: After a rebalance fix-layout, check if the rebalance status command
#displays the appropriate message at the CLI.

. $(dirname $0)/../../include.rc
. $(dirname $0)/../../volume.rc

cleanup;

#Basic checks
TEST glusterd
TEST pidof glusterd
TEST $CLI volume info

#Create a 2x1 distributed volume
TEST $CLI volume create $V0 $H0:$B0/${V0}{1,2};
TEST $CLI volume start $V0

# Mount FUSE and create file/directory
TEST glusterfs -s $H0 --volfile-id $V0 $M0
for i in `seq 1 10`;
do
       mkdir $M0/dir_$i
       echo file>$M0/dir_$i/file_$i
       for j in `seq 1 100`;
       do
                mkdir $M0/dir_$i/dir_$j
                echo file>$M0/dir_$i/dir_$j/file_$j
       done
done

#add 2 bricks
TEST $CLI volume add-brick $V0 $H0:$B0/${V0}{3,4};

#perform rebalance fix-layout
TEST $CLI volume rebalance $V0 fix-layout start

EXPECT_WITHIN $REBALANCE_TIMEOUT "fix-layout completed" fix-layout_status_field $V0;

EXPECT_WITHIN $UMOUNT_TIMEOUT "Y" force_umount $M0
TEST $CLI volume stop $V0
TEST $CLI volume delete $V0;
TEST ! $CLI volume info $V0;

cleanup;
