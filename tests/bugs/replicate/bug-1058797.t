#!/bin/bash
#Test that the setuid bit is healed correctly.

. $(dirname $0)/../../include.rc
. $(dirname $0)/../../volume.rc

cleanup;
#Basic checks
TEST glusterd

#Create a 1x2 replica volume
TEST $CLI volume create $V0 replica 2 $H0:$B0/brick{0,1};
TEST $CLI volume start $V0
TEST $CLI volume set $V0 cluster.self-heal-daemon off

# FUSE mount;create a file
TEST glusterfs -s $H0 --volfile-id $V0 $M0
TEST touch $M0/file

#Kill brick1 and set S_ISUID and S_ISGID bits from mount point
kill_brick $V0 $H0 $B0/brick1
TEST chmod +x,+s $M0/file

#Get file permissions from backend brick0 and verify that S_ISUID is indeed set
file_permissions1=`ls -l $B0/brick0/file | awk '{print $1}'| cut -d. -f1 | cut -d- -f2,3,4,5,6`
setuid_bit1=`echo $file_permissions1 | cut -b3`
EXPECT "s" echo $setuid_bit1

#Restart volume and do lookup from mount to trigger heal
TEST $CLI volume start $V0 force
EXPECT_WITHIN $CHILD_UP_TIMEOUT "1" afr_child_up_status $V0 1
TEST dd if=$M0/file of=/dev/null

#Get file permissions from healed brick1 and verify that S_ISUID is indeed set
file_permissions2=`ls -l $B0/brick1/file | awk '{print $1}' | cut -d. -f1 | cut -d- -f2,3,4,5,6`
setuid_bit2=`echo $file_permissions2 | cut -b3`
EXPECT "s" echo $setuid_bit2

#Also compare the entire permission string,just to be sure
EXPECT $file_permissions1 echo $file_permissions2
EXPECT_WITHIN $UMOUNT_TIMEOUT "Y" force_umount $M0
TEST $CLI volume stop $V0
TEST $CLI volume delete $V0;

cleanup;
