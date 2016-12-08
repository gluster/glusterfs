#!/bin/bash

. $(dirname $0)/../../include.rc
. $(dirname $0)/../../volume.rc
. $(dirname $0)/../../fallocate.rc

cleanup

require_fallocate -l 1m $M0/file
require_fallocate -p -l 512k $M0/file && rm -f $M0/file
require_fallocate -z -l 512k $M0/file && rm -f $M0/file

TEST glusterd
TEST pidof glusterd
TEST $CLI volume create $V0 replica 2 $H0:$B0/${V0}{0,1,2,3}
TEST $CLI volume set $V0 features.shard on
TEST $CLI volume start $V0

TEST $GFS --volfile-id=$V0 --volfile-server=$H0 $M0
TEST build_tester $(dirname $0)/shard-fallocate.c -lgfapi -Wall -O2

# On file1 confirm that when fallocate's offset + len > cur file size,
# the new file size will increase.
TEST touch $M0/tmp
TEST `echo 'abcdefghijklmnopqrstuvwxyz' > $M0/tmp`
TEST touch $M0/file1

gfid_file1=$(get_gfid_string $M0/file1)

TEST $(dirname $0)/shard-fallocate $H0 $V0 "0" "0" "6291456" /file1 `gluster --print-logdir`/glfs-$V0.log

EXPECT '6291456' stat -c %s $M0/file1

# This should ensure /.shard is created on the bricks.
TEST stat $B0/${V0}0/.shard
TEST stat $B0/${V0}1/.shard
TEST stat $B0/${V0}2/.shard
TEST stat $B0/${V0}3/.shard

EXPECT "2097152" echo `find $B0 -name $gfid_file1.1 | xargs stat -c %s`
EXPECT "1" file_all_zeroes $M0/file1


# On file2 confirm that fallocate to already allocated region of the
# file does not change the content of the file.
TEST truncate -s 6M $M0/file2
TEST dd if=$M0/tmp of=$M0/file2 bs=1 seek=3145728 count=26 conv=notrunc
md5sum_file2=$(md5sum $M0/file2 | awk '{print $1}')

TEST $(dirname $0)/shard-fallocate $H0 $V0 "0" "3145728" "26" /file2 `gluster --print-logdir`/glfs-$V0.log

EXPECT '6291456' stat -c %s $M0/file2
EXPECT "$md5sum_file2" echo `md5sum $M0/file2 | awk '{print $1}'`

# On file3 confirm that fallocate to a region of the file that consists
#of holes creates a new shard in its place, fallocates it and there is no
#change in the file content seen by the application.
TEST touch $M0/file3

gfid_file3=$(get_gfid_string $M0/file3)

TEST dd if=$M0/tmp of=$M0/file3 bs=1 seek=9437184 count=26 conv=notrunc
TEST ! stat $B0/$V0*/.shard/$gfid_file3.1
TEST   stat $B0/$V0*/.shard/$gfid_file3.2
md5sum_file3=$(md5sum $M0/file3 | awk '{print $1}')
EXPECT "1048602" echo `find $B0 -name $gfid_file3.2 | xargs stat -c %s`

TEST $(dirname $0)/shard-fallocate $H0 $V0 "0" "5242880" "1048576" /file3 `gluster --print-logdir`/glfs-$V0.log
EXPECT "$md5sum_file3" echo `md5sum $M0/file3 | awk '{print $1}'`

EXPECT_WITHIN $UMOUNT_TIMEOUT "Y" force_umount $M0
TEST $CLI volume stop $V0
TEST $CLI volume delete $V0
rm -f $(dirname $0)/shard-fallocate
cleanup
