#!/bin/bash

. $(dirname $0)/../include.rc
. $(dirname $0)/../volume.rc

cleanup;

TEST glusterd
TEST pidof glusterd

#Create a disperse volume
TEST $CLI volume create $V0 disperse 6 redundancy 2 $H0:$B0/${V0}{0..5}
TEST $CLI volume start $V0
EXPECT_WITHIN $PROCESS_UP_TIMEOUT 'Started' volinfo_field $V0 'Status'

#Disable md-cache
TEST $CLI volume set $V0 performance.stat-prefetch off

#Mount the volume
TEST $GFS -s $H0 --volfile-id $V0 $M0
EXPECT_WITHIN $CHILD_UP_TIMEOUT "6" ec_child_up_count $V0 0

#Enable bitrot
TEST $CLI volume bitrot $V0 enable
EXPECT_WITHIN $PROCESS_UP_TIMEOUT "1" get_bitd_count

#Create sample file
TEST `echo "1234" > $M0/FILE1`
#Create hardlink
TEST `ln $M0/FILE1 $M0/HL_FILE1`

#Corrupt file from back-end
TEST stat $B0/${V0}5/FILE1
SIZE=$(stat -c %s $B0/${V0}5/FILE1)
echo "Corrupted data" >> $B0/${V0}5/FILE1
gfid1=$(getfattr -n glusterfs.gfid.string --only-values $M0/FILE1)

#Manually set bad-file xattr
TEST setfattr -n trusted.bit-rot.bad-file -v 0x3100 $B0/${V0}5/FILE1
TEST touch "$B0/${V0}5/.glusterfs/quarantine/$gfid1"
TEST chmod 000 "$B0/${V0}5/.glusterfs/quarantine/$gfid1"
EXPECT "3" get_quarantine_count "$B0/${V0}5";

TEST $CLI volume stop $V0
TEST $CLI volume start $V0
EXPECT_WITHIN $CHILD_UP_TIMEOUT "6" ec_child_up_count $V0 0
EXPECT_WITHIN $PROCESS_UP_TIMEOUT "1" get_bitd_count

#Delete file and all links from backend
TEST rm -rf $(find $B0/${V0}5 -inum $(stat -c %i $B0/${V0}5/FILE1))

#Access files
TEST cat $M0/FILE1
EXPECT_WITHIN $HEAL_TIMEOUT "$SIZE" path_size $B0/${V0}5/FILE1
TEST cat $M0/HL_FILE1
EXPECT_WITHIN $HEAL_TIMEOUT "$SIZE" path_size $B0/${V0}5/HL_FILE1

cleanup;
