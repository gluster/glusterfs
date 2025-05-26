#!/bin/bash

. $(dirname $0)/../../include.rc
. $(dirname $0)/../../volume.rc
. $(dirname $0)/../../ec.rc

function compare_brick_stats {
    file_name=$1

    SIZE=$(stat -c%s $B0/${V0}0/$file_name)
    BLOCKS=$(stat -c%b $B0/${V0}0/$file_name)

    echo $SIZE
    echo $BLOCKS

    for b in {0..5}; do
        SIZE_IN_BRICK=$(stat -c%s $B0/${V0}${b}/$file_name)
        BLOCKS_IN_BRICK=$(stat -c%b $B0/${V0}${b}/$file_name)

        if [[ "$SIZE" -ne "$SIZE_IN_BRICK" || "$BLOCKS" -ne "$BLOCKS_IN_BRICK" ]]; then
            return  1
        fi
    done

    return 0
}

function compare_md5sum {

    if [[ "$1" == "$2" ]]; then
        return 0
    fi

    return 1
}


cleanup


TEST_DIR="/tmp/glusterfs-sparse-test"
mkdir -p $TEST_DIR


TEST glusterd
TEST pidof glusterd


TEST $CLI volume create $V0 disperse 6 redundancy 2 $H0:$B0/${V0}{0..5}
TEST $CLI volume start $V0

TEST $GFS -s $H0 --volfile-id $V0 $M0
EXPECT_WITHIN $CHILD_UP_TIMEOUT "6" ec_child_up_count $V0 0

TEST kill_brick $V0 $H0 $B0/${V0}0
TEST kill_brick $V0 $H0 $B0/${V0}1

TEST_FILE="sparse_test_file"

TEST dd if=/dev/zero of=$M0/$TEST_FILE bs=1024 count=1024 seek=0
TEST dd if=/dev/urandom of=$M0/$TEST_FILE bs=1024 count=1 seek=0 conv=notrunc
TEST dd if=/dev/urandom of=$M0/$TEST_FILE bs=1024 count=1 seek=512 conv=notrunc
TEST dd if=/dev/urandom of=$M0/$TEST_FILE bs=1024 count=1 seek=1023 conv=notrunc

# Create another sparse file with different pattern
TEST_FILE2="sparse_test_file2"
TEST truncate -s 5M $M0/$TEST_FILE2
TEST dd if=/dev/urandom of=$M0/$TEST_FILE2 bs=4096 count=1 seek=100 conv=notrunc
TEST dd if=/dev/urandom of=$M0/$TEST_FILE2 bs=4096 count=1 seek=500 conv=notrunc

sleep 2

TEST $CLI volume start $V0 force
EXPECT_WITHIN $CHILD_UP_TIMEOUT "6" ec_child_up_count $V0 0

TEST $CLI volume heal $V0
EXPECT_WITHIN $HEAL_TIMEOUT "0" get_pending_heal_count $V0

TEST compare_brick_stats $TEST_FILE
TEST compare_brick_stats $TEST_FILE2


EXPECT_WITHIN $UMOUNT_TIMEOUT "Y" force_umount $M0
$GFS --xlator-option="*.ec-read-mask=5:2:3:4" -s $H0 --volfile-id $V0 $M0
TEST $GFS -s $H0 --volfile-id $V0 $M0
EXPECT_WITHIN $CHILD_UP_TIMEOUT "6" ec_child_up_count $V0 0
checksum1=$(md5sum $M0/$TEST_FILE | cut -d' ' -f1)

EXPECT_WITHIN $UMOUNT_TIMEOUT "Y" force_umount $M0
$GFS --xlator-option="*.ec-read-mask=0:2:3:4" -s $H0 --volfile-id $V0 $M0
TEST $GFS -s $H0 --volfile-id $V0 $M0
EXPECT_WITHIN $CHILD_UP_TIMEOUT "6" ec_child_up_count $V0 0
checksum2=$(md5sum $M0/$TEST_FILE | cut -d' ' -f1)

EXPECT_WITHIN $UMOUNT_TIMEOUT "Y" force_umount $M0
$GFS --xlator-option="*.ec-read-mask=1:2:3:4" -s $H0 --volfile-id $V0 $M0
TEST $GFS -s $H0 --volfile-id $V0 $M0
EXPECT_WITHIN $CHILD_UP_TIMEOUT "6" ec_child_up_count $V0 0
checksum3=$(md5sum $M0/$TEST_FILE | cut -d' ' -f1)

TEST compare_md5sum $checksum1 $checksum2
TEST compare_md5sum $checksum1 $checksum3


#Test hole punching case
TEST_FILE3="to_be_sparsefile"
TEST dd if=/dev/urandom of=$M0/$TEST_FILE3 bs=1M count=100

TEST kill_brick $V0 $H0 $B0/${V0}0
TEST kill_brick $V0 $H0 $B0/${V0}1

TEST fallocate --punch-hole --keep-size -o 6144 -l 15728640 $M0/$TEST_FILE3


TEST $CLI volume start $V0 force
EXPECT_WITHIN $CHILD_UP_TIMEOUT "6" ec_child_up_count $V0 0

TEST $CLI volume heal $V0
EXPECT_WITHIN $HEAL_TIMEOUT "0" get_pending_heal_count $V0

TEST compare_brick_stats $TEST_FILE3

EXPECT_WITHIN $UMOUNT_TIMEOUT "Y" force_umount $M0
$GFS --xlator-option="*.ec-read-mask=5:2:3:4" -s $H0 --volfile-id $V0 $M0
TEST $GFS -s $H0 --volfile-id $V0 $M0
EXPECT_WITHIN $CHILD_UP_TIMEOUT "6" ec_child_up_count $V0 0
checksum1=$(md5sum $M0/$TEST_FILE3 | cut -d' ' -f1)

EXPECT_WITHIN $UMOUNT_TIMEOUT "Y" force_umount $M0
$GFS --xlator-option="*.ec-read-mask=0:2:3:4" -s $H0 --volfile-id $V0 $M0
TEST $GFS -s $H0 --volfile-id $V0 $M0
EXPECT_WITHIN $CHILD_UP_TIMEOUT "6" ec_child_up_count $V0 0
checksum2=$(md5sum $M0/$TEST_FILE3 | cut -d' ' -f1)

EXPECT_WITHIN $UMOUNT_TIMEOUT "Y" force_umount $M0
$GFS --xlator-option="*.ec-read-mask=1:2:3:4" -s $H0 --volfile-id $V0 $M0
TEST $GFS -s $H0 --volfile-id $V0 $M0
EXPECT_WITHIN $CHILD_UP_TIMEOUT "6" ec_child_up_count $V0 0
checksum3=$(md5sum $M0/$TEST_FILE3 | cut -d' ' -f1)

TEST compare_md5sum $checksum1 $checksum2
TEST compare_md5sum $checksum1 $checksum3


# Cleanup
rm -rf $TEST_DIR
cleanup