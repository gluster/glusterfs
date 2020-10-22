#!/bin/bash

. $(dirname $0)/../../include.rc
. $(dirname $0)/../../volume.rc

#This tests checks if directory rebalance works as expected
TESTS_EXPECTED_IN_LOOP=60

cleanup;

function verify_checksum {
    local f="$1"
    local cksum="$2"
    f_cksum=$(md5sum $f | awk '{print $1}')
    [[ "x$f_cksum" == "x$cksum" ]]
}

#https://stackoverflow.com/questions/5799303/print-a-character-repeatedly-in-bash/17030976
#'a' repeated 255 times to test that the index/log files are generated correctly
DIR1=$(printf "%0.sa" $(seq 1 255))

TEST glusterd
TEST pidof glusterd
TEST $CLI volume create $V0 $H0:$B0/${V0}0
TEST $CLI volume set $V0 performance.quick-read off
TEST $CLI volume set $V0 performance.io-cache off
TEST $CLI volume set $V0 performance.write-behind off
TEST $CLI volume set $V0 performance.stat-prefetch off
TEST $CLI volume set $V0 performance.read-ahead off
TEST $CLI volume start $V0
TEST $GFS --volfile-id=/$V0 --volfile-server=$H0 $M0
TEST mkdir $M0/${DIR1}
for i in {1..10}
do
    TEST_IN_LOOP dd if=/dev/zero of=$M0/$i bs=1k count=1
    TEST_IN_LOOP dd if=/dev/zero of=$M0/${DIR1}/$i bs=1k count=1
    TEST_IN_LOOP mknod $M0/block-${i} b 4 5
    TEST_IN_LOOP "touch $M0/${i}\*\)\(\'" #Files with special chars in them to check that the index is built correctly and files are migrated correctly
done
checksum=$(md5sum $M0/1 | awk '{print $1}')
TEST $CLI volume add-brick $V0 $H0:$B0/${V0}1 force
TEST $CLI volume rebalance $V0 fix-layout start
EXPECT_WITHIN $REBALANCE_TIMEOUT "fix-layout completed" fix-layout_status_field $V0
TEST $(dirname $0)/../../../extras/rebalance/directory-rebalance.py $M0/${DIR1}

root_file_scanned_count=0
dir_file_scanned_count=0
dir_file_migrated_count=0
for i in {1..10}
do
    #root directory files shouldn't be migrated
    if [ -f $B0/${V0}0/$i ]
    then
        root_file_scanned_count=$((root_file_scanned_count+1))
    fi

    #Some files in ${DIR1} should be migrated
    if [ -f $B0/${V0}0/${DIR1}/$i ]
    then
        dir_file_scanned_count=$((dir_file_scanned_count+1))
    else
        dir_file_migrated_count=$((dir_file_migrated_count+1))
        if [ -f $B0/${V0}1/${DIR1}/$i ]
        then
            dir_file_scanned_count=$((dir_file_scanned_count+1))
        fi
    fi
done

EXPECT "^10$" echo $root_file_scanned_count
EXPECT "^10$" echo $dir_file_scanned_count
EXPECT_NOT "^0$" echo $dir_file_migrated_count
TEST $(dirname $0)/../../../extras/rebalance/directory-rebalance.py $M0

root_file_scanned_count=0
root_file_migrated_count=0
root_block_success_count=0
root_block_failure_count=0
for i in {1..10}
do
    if [ -e $B0/${V0}0/block-${i} ]
    then
        root_block_success_count=$((root_block_success_count+1))
    else
        root_block_failure_count=$((root_block_failure_count+1))
        if [ -e $B0/${V0}1/block-${i} ]
        then
            root_block_success_count=$((root_block_success_count+1))
        fi
    fi

    #Some files in root should be migrated
    if [ -f $B0/${V0}0/$i ]
    then
        root_file_scanned_count=$((root_file_scanned_count+1))
    else
        root_file_migrated_count=$((root_file_migrated_count+1))
        if [ -f $B0/${V0}1/$i ]
        then
            root_file_scanned_count=$((root_file_scanned_count+1))
        fi
    fi
done

EXPECT "^10$" echo $root_file_scanned_count
EXPECT_NOT "^0$" echo $root_file_migrated_count
#special files also should be migrated
EXPECT "^10$" echo $root_block_success_count
EXPECT_NOT "^0$" echo $root_blocks_failure_count

#If we trigger the rebalance command now, nothing should be rebalanced
#Test that rebalance crawl is equivalent to manual rebalance
TEST $CLI volume rebalance $V0 start
EXPECT_WITHIN $REBALANCE_TIMEOUT "completed" rebalance_status_field $V0
EXPECT "^0$" rebalanced_files_field $V0
EXPECT "^0Bytes$" rebalanced_size_field $V0

#-ve tests
#Script should fail on non-existent path
TEST ! $(dirname $0)/../../../extras/rebalance/directory-rebalance.py i-dont-exist

EXPECT_WITHIN $UMOUNT_TIMEOUT "Y" force_umount $M0
TEST $GFS --volfile-id=/$V0 --volfile-server=$H0 $M0
for i in {1..10}
do
    TEST_IN_LOOP verify_checksum $M0/$i $checksum
    TEST_IN_LOOP verify_checksum $M0/${DIR1}/$i $checksum
done
cleanup
