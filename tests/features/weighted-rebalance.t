#!/bin/bash

. $(dirname $0)/../include.rc
. $(dirname $0)/../volume.rc
. $(dirname $0)/../dht.rc

NFILES=1000

touch_files () {
        for i in $(seq 1 $NFILES); do
                touch $(printf $M0/dir/file%02d $i) 2> /dev/null
        done
}

count_files () {
        found=0
        for i in $(seq 1 $NFILES); do
                if [ -f $(printf $1/dir/file%02d $i) ]; then
                        found=$((found+1))
                fi
        done
        echo $found
}

get_xattr () {
        cmd="getfattr --absolute-names --only-values -n trusted.glusterfs.dht"
        $cmd $1 | od -tx1 -An | tr -d ' '
}

cleanup

TEST glusterd
TEST pidof glusterd
TEST $CLI volume info

TEST mkdir ${B0}/${V0}{1,2}

TEST truncate -s $((40*1024*1024)) ${B0}/disk1
TEST MKFS_LOOP -i 512 ${B0}/disk1
TEST MOUNT_LOOP ${B0}/disk1 ${B0}/${V0}1

TEST truncate -s $((80*1024*1024)) ${B0}/disk2
TEST MKFS_LOOP -i 512 ${B0}/disk2
TEST MOUNT_LOOP ${B0}/disk2 ${B0}/${V0}2

TEST $CLI volume create $V0 $H0:$B0/${V0}{1,2}
EXPECT "$V0" volinfo_field $V0 'Volume Name'
EXPECT 'Created' volinfo_field $V0 'Status'

TEST $CLI volume start $V0
EXPECT 'Started' volinfo_field $V0 'Status'

# Create some files for later tests.
TEST $GFS -s $H0 --volfile-id $V0 $M0
TEST mkdir $M0/dir
TEST touch_files
EXPECT_WITHIN $UMOUNT_TIMEOUT "Y" force_umount $M0

# Check that the larger brick got more of the files.
nfiles=$(count_files ${B0}/${V0}2)
#echo $nfiles $(get_xattr ${B0}/${V0}1) $(get_xattr ${B0}/${V0}2) 3>&2 2>&1 1>&3 3>&-
TEST [ $nfiles -ge 580 ]

# Turn off the size-weighted rebalance.
TEST $CLI volume set $V0 cluster.weighted-rebalance off

# Rebalance again and check that the distribution is even again.
TEST $CLI volume rebalance $V0 start force
EXPECT_WITHIN $REBALANCE_TIMEOUT "0" rebalance_completed
nfiles=$(count_files ${B0}/${V0}2)
#echo $nfiles $(get_xattr ${B0}/${V0}1) $(get_xattr ${B0}/${V0}2) 3>&2 2>&1 1>&3 3>&-
TEST [ $nfiles -le 580 ]

$CLI volume stop $V0
UMOUNT_LOOP ${B0}/${V0}{1,2}
rm -f ${B0}/disk{1,2}

cleanup
