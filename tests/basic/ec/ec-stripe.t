#!/bin/bash

. $(dirname $0)/../../include.rc
. $(dirname $0)/../../volume.rc

# These tests will check the stripe cache functionality of
# disperse volume

test_index=0
stripe_count=4
loop_test=0

TESTS_EXPECTED_IN_LOOP=182

function get_mount_stripe_cache {
        local sd=$1
        local field=$2
        local val=$(grep "$field" $sd | cut -f2 -d'=' | tail -1)
        echo $val
}

function get_stripes_in_cache {
        local target=$1
        local count=$2
        local c=0
        for (( c=0; c<$count; c++ ))
        do
            let x=102+$c*1024
            echo yy | dd of=$target oflag=seek_bytes,sync seek=$x conv=notrunc
            if [ $? != 0 ]
            then
                break
            fi
        done
        echo "$c"
}
# tests in this loop = 7
function mount_get_test_files {
        let test_index+=1
        let loop_test+=7
        echo "Test Case $test_index"
        local stripe_count=$1
        TEST $GFS --volfile-id=/$V0 --volfile-server=$H0 $M0;
        EXPECT_WITHIN $CHILD_UP_TIMEOUT "3" ec_child_up_count $V0 0
        TEST dd if=/dev/urandom of=$B0/test_file bs=1024 count=20
        TEST cp $B0/test_file $M0/test_file
        TEST dd if=/dev/urandom of=$B0/misc_file bs=1024 count=20
        EXPECT_WITHIN $UMOUNT_TIMEOUT "$stripe_count" get_stripes_in_cache $B0/test_file $stripe_count
        EXPECT_WITHIN $UMOUNT_TIMEOUT "$stripe_count" get_stripes_in_cache $M0/test_file $stripe_count
}

#check_statedump_md5sum (hitcount misscount)
#tests in this loop = 4
function check_statedump_md5sum {
        statedump=$(generate_mount_statedump $V0)
        let loop_test+=4
        sleep 1
        nhits=$(get_mount_stripe_cache $statedump "hits")
        nmisses=$(get_mount_stripe_cache $statedump "misses")
        EXPECT "$1" echo $nhits
        EXPECT "$2" echo $nmisses
        TEST md5_sum=`get_md5_sum $B0/test_file`
        EXPECT $md5_sum get_md5_sum $M0/test_file
}

#tests in this loop = 2
function clean_file_unmount {
        let loop_test+=2
        TEST rm -f $B0/test_file $M0/test_file $B0/misc_file
        cleanup_mount_statedump $V0
        EXPECT_WITHIN $UMOUNT_TIMEOUT "Y" force_umount $M0
}

cleanup
TEST glusterd
TEST pidof glusterd
TEST $CLI volume create $V0 disperse 3 redundancy 1 $H0:$B0/${V0}{0..2}
TEST $CLI volume heal $V0 disable
TEST $CLI volume set $V0 performance.stat-prefetch off
TEST $CLI volume set $V0 performance.write-behind off
TEST $CLI volume set $V0 performance.quick-read off
TEST $CLI volume set $V0 performance.read-ahead off
TEST $CLI volume set $V0 performance.io-cache off
TEST $CLI volume set $V0 disperse.background-heals 0
TEST $CLI volume set $V0 disperse.eager-lock on
TEST $CLI volume set $V0 disperse.other-eager-lock on
TEST $CLI volume set $V0 disperse.stripe-cache 8
TEST $CLI volume start $V0

### 1 - offset and size in one stripes ####

mount_get_test_files $stripe_count
# This should have 4 hits on cached stripes
get_stripes_in_cache $M0/test_file $stripe_count
check_statedump_md5sum 4 4
clean_file_unmount

### 2 - Length less than a stripe size, covering two stripes ####

mount_get_test_files $stripe_count
TEST dd if=$B0/misc_file of=$B0/test_file  bs=1022 count=1  oflag=seek_bytes,sync seek=102 conv=notrunc
TEST dd if=$B0/misc_file of=$M0/test_file  bs=1022 count=1  oflag=seek_bytes,sync seek=102 conv=notrunc
check_statedump_md5sum 2 4
clean_file_unmount

### 3 -Length exactly equal to the stripe size, covering a single stripe  ####

mount_get_test_files $stripe_count
TEST dd if=$B0/misc_file of=$B0/test_file  bs=1024 count=1  oflag=seek_bytes,sync seek=0 conv=notrunc
TEST dd if=$B0/misc_file of=$M0/test_file  bs=1024 count=1  oflag=seek_bytes,sync seek=0 conv=notrunc
check_statedump_md5sum 0 4
clean_file_unmount

### 4 - Length exactly equal to the stripe size, covering two stripes  ####

mount_get_test_files $stripe_count
TEST dd if=$B0/misc_file of=$B0/test_file  bs=2048 count=1  oflag=seek_bytes,sync seek=1024 conv=notrunc
TEST dd if=$B0/misc_file of=$M0/test_file  bs=2048 count=1  oflag=seek_bytes,sync seek=1024 conv=notrunc
check_statedump_md5sum 0 4
clean_file_unmount

### 5 - Length greater than a stripe size, covering two stripes  ####

mount_get_test_files $stripe_count
TEST dd if=$B0/misc_file of=$B0/test_file  bs=1030 count=1  oflag=seek_bytes,sync seek=500 conv=notrunc
TEST dd if=$B0/misc_file of=$M0/test_file  bs=1030 count=1  oflag=seek_bytes,sync seek=500 conv=notrunc
check_statedump_md5sum 2 4
clean_file_unmount

### 6 - Length greater than a stripe size, covering three stripes  ####

mount_get_test_files $stripe_count
TEST dd if=$B0/misc_file of=$B0/test_file  bs=2078 count=1  oflag=seek_bytes,sync seek=1000 conv=notrunc
TEST dd if=$B0/misc_file of=$M0/test_file  bs=2078 count=1  oflag=seek_bytes,sync seek=1000 conv=notrunc
check_statedump_md5sum 2 4
clean_file_unmount

### 7 - Discard range - all stripe from cache should be invalidated complete stripes  ####

mount_get_test_files $stripe_count
TEST fallocate -p -o 0 -l 5120 $B0/test_file
TEST fallocate -p -o 0 -l 5120 $M0/test_file
TEST dd if=$B0/misc_file of=$B0/test_file  bs=1024 count=6  oflag=seek_bytes,sync seek=1030 conv=notrunc
TEST dd if=$B0/misc_file of=$M0/test_file  bs=1024 count=6  oflag=seek_bytes,sync seek=1030 conv=notrunc
check_statedump_md5sum 5 11
clean_file_unmount

### 8 - Discard range - starts in the middle of stripe, ends on the middle of next stripe####

mount_get_test_files $stripe_count
TEST fallocate -p -o 500 -l 1024 $B0/test_file
TEST fallocate -p -o 500 -l 1024 $M0/test_file
TEST dd if=$B0/misc_file of=$B0/test_file  bs=1024 count=5  oflag=seek_bytes,sync seek=500 conv=notrunc
TEST dd if=$B0/misc_file of=$M0/test_file  bs=1024 count=5  oflag=seek_bytes,sync seek=500 conv=notrunc
check_statedump_md5sum 10 6
clean_file_unmount

### 9 - Discard range - starts in the middle of stripe, ends on the middle of 3rd stripe#####

mount_get_test_files $stripe_count
TEST fallocate -p -o 500 -l 2048 $B0/test_file
TEST fallocate -p -o 500 -l 2048 $M0/test_file
TEST dd if=$B0/misc_file of=$B0/test_file  bs=1024 count=5  oflag=seek_bytes,sync seek=500 conv=notrunc
TEST dd if=$B0/misc_file of=$M0/test_file  bs=1024 count=5  oflag=seek_bytes,sync seek=500 conv=notrunc
check_statedump_md5sum 9 7
clean_file_unmount

### 10 - Discard range - starts and end within one stripe ####

mount_get_test_files $stripe_count
TEST fallocate -p -o 500 -l 100 $B0/test_file
TEST fallocate -p -o 500 -l 100 $M0/test_file
TEST dd if=$B0/misc_file of=$B0/test_file  bs=1024 count=1  oflag=seek_bytes,sync seek=0 conv=notrunc
TEST dd if=$B0/misc_file of=$M0/test_file  bs=1024 count=1  oflag=seek_bytes,sync seek=0 conv=notrunc
check_statedump_md5sum 1 4
clean_file_unmount

### 11 - Discard range - starts and end in one complete stripe ####

mount_get_test_files $stripe_count
TEST fallocate -p -o 0 -l 1024 $B0/test_file
TEST fallocate -p -o 0 -l 1024 $M0/test_file
TEST dd if=$B0/misc_file of=$B0/test_file  bs=1024 count=1  oflag=seek_bytes,sync seek=512 conv=notrunc
TEST dd if=$B0/misc_file of=$M0/test_file  bs=1024 count=1  oflag=seek_bytes,sync seek=512 conv=notrunc
check_statedump_md5sum 1 5
clean_file_unmount

### 12 - Discard range - starts and end two complete stripe ####

mount_get_test_files $stripe_count
TEST fallocate -p -o 0 -l 2048 $B0/test_file
TEST fallocate -p -o 0 -l 2048 $M0/test_file
TEST dd if=$B0/misc_file of=$B0/test_file  bs=1024 count=4  oflag=seek_bytes,sync seek=300 conv=notrunc
TEST dd if=$B0/misc_file of=$M0/test_file  bs=1024 count=4  oflag=seek_bytes,sync seek=300 conv=notrunc
check_statedump_md5sum 5 7
clean_file_unmount

### 13 - Truncate to invalidate  all the stripe in cache  ####

mount_get_test_files $stripe_count
TEST truncate -s 0 $B0/test_file
TEST truncate -s 0 $M0/test_file
TEST dd if=$B0/misc_file of=$B0/test_file  bs=1022 count=5  oflag=seek_bytes,sync seek=400 conv=notrunc
TEST dd if=$B0/misc_file of=$M0/test_file  bs=1022 count=5  oflag=seek_bytes,sync seek=400 conv=notrunc
check_statedump_md5sum 4 5
clean_file_unmount

### 14 - Truncate to invalidate  all but one the stripe in cache  ####

mount_get_test_files $stripe_count
TEST truncate -s 500 $B0/test_file
TEST truncate -s 500 $M0/test_file
TEST dd if=$B0/misc_file of=$B0/test_file  bs=1024 count=1  oflag=seek_bytes,sync seek=525 conv=notrunc
TEST dd if=$B0/misc_file of=$M0/test_file  bs=1024 count=1  oflag=seek_bytes,sync seek=525 conv=notrunc
check_statedump_md5sum  2 4
clean_file_unmount

### 15 - Truncate to invalidate  all but one the stripe in cache  ####
mount_get_test_files $stripe_count
TEST truncate -s 2148 $B0/test_file
TEST truncate -s 2148 $M0/test_file
TEST dd if=$B0/misc_file of=$B0/test_file  bs=1000 count=1  oflag=seek_bytes,sync seek=2050 conv=notrunc
TEST dd if=$B0/misc_file of=$M0/test_file  bs=1000 count=1  oflag=seek_bytes,sync seek=2050 conv=notrunc
check_statedump_md5sum 2 4
clean_file_unmount
echo "Total loop tests $loop_test"
cleanup
