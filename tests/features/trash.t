#!/bin/bash

. $(dirname $0)/../include.rc
. $(dirname $0)/../volume.rc

cleanup

test_mount() {
        volume=$1
        mount=$2
        test_dir=$3
        RETVAL=0
        glusterfs -s $H0 --volfile-id $volume $mount --attribute-timeout=0

        if [ "x$test_dir" = "x" ] ; then return $RETVAL; fi

        timeout=0
        while [ $timeout -lt $PROCESS_UP_TIMEOUT ] ; do
                timeout=$(( $timeout + 1 ))
                test -d $test_dir
                RETVAL=$?
                if [ $RETVAL -eq 0 ] ; then break ; fi
                sleep 1
        done

        return $RETVAL
}

start_vol() {
        volume=$1
        mount=$2
        test_dir=$3
        $CLI volume start $volume
        test_mount $volume $mount $test_dir
        RETVAL=$?
        return $RETVAL
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

# test without enabling trash translator [8]
TEST start_vol $V0 $M0

# test on enabling trash translator [9-10]
TEST $CLI volume set $V0 features.trash on
EXPECT 'on' volinfo_field $V0 'features.trash'

# files directly under mount point [11]
create_files $M0/file1 $M0/file2
TEST file_exists $V0 file1 file2

# perform unlink [12]
TEST unlink_op file1

# perform truncate [13]
TEST truncate_op file2 4

# create files directory hierarchy and check [14]
mkdir -p $M0/1/2/3
create_files $M0/1/2/3/foo1 $M0/1/2/3/foo2
TEST file_exists $V0 1/2/3/foo1 1/2/3/foo2

# perform unlink [15]
TEST unlink_op 1/2/3/foo1

# perform truncate [16]
TEST truncate_op 1/2/3/foo2 4

# create a directory for eliminate pattern
mkdir $M0/a

# set the eliminate pattern [17-18]
TEST $CLI volume set $V0 features.trash-eliminate-path /a
EXPECT '/a' volinfo_field $V0 'features.trash-eliminate-path'

# create two files and check [19]
create_files $M0/a/test1 $M0/a/test2
TEST file_exists $V0 a/test1 a/test2

# remove from eliminate pattern [20]
rm -f $M0/a/test1
EXPECT "Y" wildcard_not_exists $M0/.trashcan/a/test1*

# truncate from eliminate path [21-23]
truncate -s 2 $M0/a/test2
TEST [ -e $M0/a/test2 ]
TEST [ `ls -l $M0/a/test2 | awk '{print $5}'` -eq 2 ]
EXPECT "Y" wildcard_not_exists $M0/.trashcan/a/test2*

# set internal op on [24-25]
TEST $CLI volume set $V0 features.trash-internal-op on
EXPECT 'on' volinfo_field $V0 'features.trash-internal-op'

# again create two files and check [26]
create_files $M0/inop1 $M0/inop2
TEST file_exists $V0 inop1 inop2

# perform unlink [27]
TEST unlink_op inop1

# perform truncate [28]
TEST truncate_op inop2 4

# remove one brick and restart the volume [28-31]
TEST $CLI volume remove-brick $V0 $H0:$B0/${V0}2 force
EXPECT_WITHIN $UMOUNT_TIMEOUT "Y" force_umount $M0
TEST $CLI volume stop $V0
TEST start_vol $V0 $M0 $M0/.trashcan

# again create two files and check [33]
create_files $M0/rebal1 $M0/rebal2
TEST file_exists $V0 rebal1 rebal2

# add one brick [34-35]
TEST $CLI volume add-brick $V0 $H0:$B0/${V0}3
TEST [ -d $B0/${V0}3 ]

# perform rebalance [36]
TEST $CLI volume rebalance $V0 start force

# check whether rebalance was succesful [37-40]
EXPECT_WITHIN $REBALANCE_TIMEOUT "Y" wildcard_exists $B0/${V0}3/rebal2
EXPECT_WITHIN $REBALANCE_TIMEOUT "Y" wildcard_exists $B0/${V0}1/.trashcan/internal_op/rebal2*
EXPECT_WITHIN $UMOUNT_TIMEOUT "Y" force_umount $M0
# force required in case rebalance is not over
TEST $CLI volume stop $V0 force

# create a replicated volume [41]
TEST $CLI volume create $V1 replica 2 $H0:$B0/${V1}{1,2}

# checking volume status [42-45]
EXPECT "$V1" volinfo_field $V1 'Volume Name'
EXPECT 'Replicate' volinfo_field $V1 'Type'
EXPECT 'Created' volinfo_field $V1 'Status'
EXPECT '2' brick_count $V1

# enable trash with options and start the replicate volume by disabling automatic self-heal [46-50]
TEST $CLI volume set $V1 features.trash on
TEST $CLI volume set $V1 features.trash-internal-op on
EXPECT 'on' volinfo_field $V1 'features.trash'
EXPECT 'on' volinfo_field $V1 'features.trash-internal-op'
TEST start_vol $V1 $M1 $M1/.trashcan

# mount and check for trash directory [51]
TEST [ -d $M1/.trashcan/internal_op ]

# create a file and check [52]
touch $M1/self
TEST [ -e $B0/${V1}1/self -a -e $B0/${V1}2/self ]

# kill one brick and delete the file from mount point [53-54]
kill_brick $V1 $H0 $B0/${V1}1
EXPECT_WITHIN ${PROCESS_UP_TIMEOUT} "1" online_brick_count
rm -f $M1/self
EXPECT "Y" wildcard_exists $B0/${V1}2/.trashcan/self*

# force start the volume and trigger the self-heal manually [55-57]
TEST $CLI volume start $V1 force
EXPECT_WITHIN $PROCESS_UP_TIMEOUT "2" online_brick_count
EXPECT_WITHIN $PROCESS_UP_TIMEOUT "Y" glustershd_up_status
# Since we created the file under root of the volume, it will be
# healed automatically

# check for the removed file in trashcan [58]
EXPECT_WITHIN $HEAL_TIMEOUT "Y" wildcard_exists $B0/${V1}1/.trashcan/internal_op/self*

# check renaming of trash directory through cli [59-62]
TEST $CLI volume set $V0 trash-dir abc
TEST start_vol $V0 $M0 $M0/abc
TEST [ -e $M0/abc -a ! -e $M0/.trashcan ]
EXPECT "Y" wildcard_exists $B0/${V0}1/abc/internal_op/rebal2*

# ensure that rename and delete operation on trash directory fails [63-65]
rm -rf $M0/abc/internal_op
TEST [ -e $M0/abc/internal_op ]
rm -rf $M0/abc/
TEST [ -e $M0/abc ]
mv $M0/abc $M0/trash
TEST [ -e $M0/abc ]

cleanup
