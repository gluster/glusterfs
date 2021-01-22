#!/bin/bash
. $(dirname $0)/../include.rc
. $(dirname $0)/../volume.rc

cleanup;

TEST glusterd
TEST pidof glusterd
TEST $CLI volume create $V0 ${H0}:${B0}/${V0}-0
TEST $CLI volume set $V0 performance.write-behind off
TEST $CLI volume set $V0 performance.read-ahead off

# Toggling works.
TEST $CLI volume set $V0 storage.linux-io_uring on
EXPECT "on" volume_option $V0 storage.linux-io_uring
TEST $CLI volume set $V0 storage.linux-io_uring off
EXPECT "off" volume_option $V0 storage.linux-io_uring

# Toggling fails when volume is running.
TEST $CLI volume start $V0
EXPECT 'Started' volinfo_field $V0 'Status'
TEST ! $CLI volume set $V0 storage.linux-io_uring on
TEST $CLI volume stop $V0
TEST  $CLI volume set $V0 storage.linux-io_uring on
TEST $CLI volume start $V0
EXPECT 'Started' volinfo_field $V0 'Status'
EXPECT "on" volume_option $V0 storage.linux-io_uring

# Mount and perform I/O, check for FOPS.
TEST $GFS --volfile-id=$V0 --volfile-server=$H0 $M0;
TEST $CLI volume profile $V0 start

# Coverage for posix_io_uring_writev() and posix_io_uring_fsync().
TEST dd if=/dev/zero of=$M0/file count=2 bs=128k iflag=fullblock oflag=sync
EXPECT "^2" echo `$CLI volume profile $V0 info| grep -w WRITE | awk '{print $8}'`
EXPECT "^2" echo `$CLI volume profile $V0 info| grep -w FSYNC | awk '{print $8}'`

# Coverage for posix_io_uring_readv()
EXPECT_WITHIN $UMOUNT_TIMEOUT "Y" force_umount $M0
TEST $GFS --volfile-id=$V0 --volfile-server=$H0 $M0;
TEST $CLI volume profile $V0 info clear
TEST dd if=$M0/file of=/dev/null count=2 bs=128k iflag=fullblock
EXPECT "^2" echo `$CLI volume profile $V0 info| grep -w READ | awk '{print $8}'`


#****************************************************************************#
# Test integrity of aligned, unaligned, small and large block size i/o (reads/writes).
#****************************************************************************#
# Create the files first.
TEST dd if=/dev/urandom of=/tmp/aligned_small_bs bs=512 count=1 iflag=fullblock
SUM1=$(md5sum /tmp/aligned_small_bs | awk '{print $1}')
TEST dd if=/dev/urandom of=/tmp/aligned_large_bs bs=4194304 count=1 iflag=fullblock
SUM2=$(md5sum /tmp/aligned_large_bs | awk '{print $1}')
TEST dd if=/dev/urandom of=/tmp/unaligned_small_bs bs=1030 count=1 iflag=fullblock
SUM3=$(md5sum /tmp/unaligned_small_bs | awk '{print $1}')
TEST dd if=/dev/urandom of=/tmp/unaligned_large_bs bs=1000000 count=1 iflag=fullblock
SUM4=$(md5sum /tmp/unaligned_large_bs | awk '{print $1}')

# Copy the files on the volume using same access pattern
TEST dd if=/tmp/aligned_small_bs of=$M0/aligned_small_bs bs=512 count=1 iflag=fullblock
TEST dd if=/tmp/aligned_large_bs of=$M0/aligned_large_bs bs=4194304 count=1 iflag=fullblock
TEST dd if=/tmp/unaligned_small_bs of=$M0/unaligned_small_bs bs=1030 count=1 iflag=fullblock
TEST dd if=/tmp/unaligned_large_bs of=$M0/unaligned_large_bs bs=1000000 count=1 iflag=fullblock

# Verify write integrity
EXPECT "^$SUM1" echo $(md5sum ${B0}/${V0}-0/aligned_small_bs | awk '{print $1}')
EXPECT "^$SUM2" echo $(md5sum ${B0}/${V0}-0/aligned_large_bs | awk '{print $1}')
EXPECT "^$SUM3" echo $(md5sum ${B0}/${V0}-0/unaligned_small_bs | awk '{print $1}')
EXPECT "^$SUM4" echo $(md5sum ${B0}/${V0}-0/unaligned_large_bs | awk '{print $1}')

# Verify read integrity via mount
EXPECT "^$SUM1" echo $(dd if=$M0/aligned_small_bs bs=512 count=1 iflag=fullblock | md5sum | awk '{print $1}')
EXPECT "^$SUM2" echo $(dd if=$M0/aligned_large_bs bs=4194304 count=1 iflag=fullblock | md5sum | awk '{print $1}')
EXPECT "^$SUM3" echo $(dd if=$M0/unaligned_small_bs bs=1030 count=1 iflag=fullblock | md5sum | awk '{print $1}')
EXPECT "^$SUM4" echo $(dd if=$M0/unaligned_large_bs bs=1000000 count=1 iflag=fullblock | md5sum | awk '{print $1}')

#****************************************************************************#
# Test integrity of direct I/O based write and read.
#****************************************************************************#
TEST dd if=/tmp/aligned_large_bs of=$M0/aligned_large_bs bs=1048576 count=4 iflag=fullblock oflag=direct
EXPECT "^$SUM2" echo $(dd if=$M0/aligned_large_bs bs=1048576 count=4 iflag=fullblock iflag=direct| md5sum | awk '{print $1}')


TEST rm -f /tmp/aligned_small_bs /tmp/aligned_large_bs /tmp/unaligned_small_bs /tmp/unaligned_large_bs
EXPECT_WITHIN $UMOUNT_TIMEOUT "Y" force_umount $M0
TEST $CLI volume stop $V0
cleanup
