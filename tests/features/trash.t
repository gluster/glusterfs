#!/bin/bash

. $(dirname $0)/../include.rc
. $(dirname $0)/../volume.rc

cleanup

test_mount() {
        glusterfs -s $H0 --volfile-id $V0 $M0 --attribute-timeout=0
        timeout=0
        while [ $timeout -lt $PROCESS_UP_TIMEOUT ] ; do
                timeout=$(( $timeout + 1 ))
                test -d $M0/.trashcan && break
        done
}

start_vol() {
        $CLI volume start $V0
        test_mount
}

create_files() {
        echo 'Hi' > $1
        echo 'Hai' > $2
}

file_exists () {
        vol=$1
        shift
        for file in `ls $B0/${vol}1/$@ 2> /dev/null` ; do
                test -e ${file} && return 0
        done
        for file in `ls $B0/${vol}2/$@ 2> /dev/null` ; do
                test -e ${file} && return 0
        done

        return 1
}

unlink_op() {

        rm -f $M0/$1
        ls $M0/.trashcan/1/2/3 &> /dev/null
        sleep 2

        test ! -e $M0/$1
        wildcard_exists $M0/.trashcan/$1*

        # remove from trashcan
        rm -f $M0/.trashcan/$1*
        wildcard_not_exists $M0/.trashcan/$1*
}

truncate_op() {
        truncate -s 2 $M0/$1
        ls $M0/.trashcan/1/2/3 &> /dev/null
        sleep 2

        test -e $M0/$1
        test $(ls -l $M0/$1 | awk '{print $5}') -eq 2 &> /dev/null
        wildcard_exists $M0/.trashcan/$1*
        test $(ls -l $M0/.trashcan/$1*|awk '{print $5}') -eq $2 &> /dev/null

        # truncate from trashcan
        truncate -s 1 $M0/.trashcan/$1*
        test $(ls $M0/.trashcan/$1* | wc -l) -eq 1
}

wildcard_exists() {
        test -e $1
        if [ $? -eq 0 ]; then echo "Y"; else echo "N"; fi
}

wildcard_not_exists() {
        test ! -e $1
        if [ $? -eq 0 ]; then echo "Y"; else echo "N"; fi
}

# testing glusterd [1-3]
TEST glusterd
TEST pidof glusterd
TEST $CLI volume info

# creating distributed volume [4]
TEST $CLI volume create $V0 $H0:$B0/${V0}{1,2}

# checking volume status [5-7]
EXPECT "$V0" volinfo_field $V0 'Volume Name'
EXPECT 'Created' volinfo_field $V0 'Status'
EXPECT '2' brick_count $V0

# test without enabling trash translator [8-10]
TEST $CLI volume start $V0
TEST glusterfs -s $H0 --volfile-id $V0 $M0 --attribute-timeout=0
TEST [ -d $M0/.trashcan ]

# test on enabling trash translator [11-12]
TEST $CLI volume set $V0 features.trash on
EXPECT 'on' volinfo_field $V0 'features.trash'

# files directly under mount point [13]
create_files $M0/file1 $M0/file2
TEST file_exists $V0 file1 file2

# perform unlink [14]
TEST unlink_op file1

# perform truncate [15]
TEST truncate_op file2 4

# create files directory hierarchy and check [16]
mkdir -p $M0/1/2/3
create_files $M0/1/2/3/foo1 $M0/1/2/3/foo2
TEST file_exists $V0 1/2/3/foo1 1/2/3/foo2

# perform unlink [17]
TEST unlink_op 1/2/3/foo1

# perform truncate [18]
TEST truncate_op 1/2/3/foo2 4

# create a directory for eliminate pattern
mkdir $M0/a

# set the eliminate pattern [19-20]
TEST $CLI volume set $V0 features.trash-eliminate-path /a
EXPECT '/a' volinfo_field $V0 'features.trash-eliminate-path'

# create two files and check [21]
create_files $M0/a/test1 $M0/a/test2
TEST file_exists $V0 a/test1 a/test2

# remove from eliminate pattern [22]
rm -f $M0/a/test1
EXPECT "Y" wildcard_not_exists $M0/.trashcan/a/test1*

# truncate from eliminate path [23-25]
truncate -s 2 $M0/a/test2
TEST [ -e $M0/a/test2 ]
TEST [ `ls -l $M0/a/test2 | awk '{print $5}'` -eq 2 ]
EXPECT "Y" wildcard_not_exists $M0/.trashcan/a/test2*

# set internal op on [26-27]
TEST $CLI volume set $V0 features.trash-internal-op on
EXPECT 'on' volinfo_field $V0 'features.trash-internal-op'

# again create two files and check [28]
create_files $M0/inop1 $M0/inop2
TEST file_exists $V0 inop1 inop2

# perform unlink [29]
TEST unlink_op inop1

# perform truncate [30]
TEST truncate_op inop2 4

# remove one brick and restart the volume [31-34]
TEST $CLI volume remove-brick $V0 $H0:$B0/${V0}2 force
EXPECT_WITHIN $UMOUNT_TIMEOUT "Y" force_umount $M0
TEST $CLI volume stop $V0
TEST start_vol

# again create two files and check [35]
create_files $M0/rebal1 $M0/rebal2
TEST file_exists $V0 rebal1 rebal2

# add one brick [36-37]
TEST $CLI volume add-brick $V0 $H0:$B0/${V0}3
TEST [ -d $B0/${V0}3 ]

# perform rebalance [38]
TEST $CLI volume rebalance $V0 start force
sleep 3

# check whether rebalance was succesful [39-42]
TEST [ -e $B0/${V0}3/rebal2 ]
EXPECT "Y" wildcard_exists $B0/${V0}1/.trashcan/internal_op/rebal2*
EXPECT_WITHIN $UMOUNT_TIMEOUT "Y" force_umount $M0
TEST $CLI volume stop $V0

# create a replicated volume [43]
TEST $CLI volume create $V1 replica 2 $H0:$B0/${V1}{1,2}

# checking volume status [44-47]
EXPECT "$V1" volinfo_field $V1 'Volume Name'
EXPECT 'Replicate' volinfo_field $V1 'Type'
EXPECT 'Created' volinfo_field $V1 'Status'
EXPECT '2' brick_count $V1

# enable trash with options and start the replicate volume by disabling automatic self-heal [48-52]
TEST $CLI volume set $V1 features.trash on
TEST $CLI volume set $V1 features.trash-internal-op on
EXPECT 'on' volinfo_field $V1 'features.trash'
EXPECT 'on' volinfo_field $V1 'features.trash-internal-op'
TEST $CLI volume start $V1

# mount and check for trash directory [53]
glusterfs -s $H0 --volfile-id $V1 $M1 --attribute-timeout=0
TEST [ -d $M1/.trashcan/internal_op ]

# create a file and check [54]
touch $M1/self
TEST [ -e $B0/${V1}1/self -a -e $B0/${V1}2/self ]

# kill one brick and delete the file from mount point [55-56]
kill_brick $V1 $H0 $B0/${V1}1
EXPECT_WITHIN ${PROCESS_UP_TIMEOUT} "1" online_brick_count
rm -f $M1/self
EXPECT "Y" wildcard_exists $B0/${V1}2/.trashcan/self*

# force start the volume and trigger the self-heal manually [57-59]
TEST $CLI volume start $V1 force
EXPECT_WITHIN $PROCESS_UP_TIMEOUT "Y" glustershd_up_status
TEST $CLI volume heal $V1

# check for the removed file in trashcan [60]
EXPECT_WITHIN $HEAL_TIMEOUT "Y" wildcard_exists $B0/${V1}1/.trashcan/internal_op/self*

# check renaming of trash directory through cli [61-65]
TEST $CLI volume set $V0 trash-dir abc
TEST $CLI volume start $V0
TEST glusterfs -s $H0 --volfile-id $V0 $M0 --attribute-timeout=0
TEST [ -e $M0/abc -a ! -e $M0/.trashcan ]
EXPECT "Y" wildcard_exists $B0/${V0}1/abc/internal_op/rebal2*

# ensure that rename and delete operation on trash directory fails [66-68]
rm -rf $M0/abc/internal_op
TEST [ -e $M0/abc/internal_op ]
rm -rf $M0/abc/
TEST [ -e $M0/abc ]
mv $M0/abc $M0/trash
TEST [ -e $M0/abc ]

cleanup
