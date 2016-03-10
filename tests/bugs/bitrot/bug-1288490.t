#!/bin/bash

. $(dirname $0)/../../include.rc
. $(dirname $0)/../../volume.rc

cleanup;

TEST glusterd
TEST pidof glusterd

TEST $CLI volume create $V0 replica 2 $H0:$B0/brick{0,1}
TEST $CLI volume start $V0

TEST $CLI volume bitrot $V0 enable
EXPECT_WITHIN $PROCESS_UP_TIMEOUT "1" get_bitd_count

TEST $GFS --volfile-id=$V0 --volfile-server=$H0 $M0
TEST dd if=/dev/urandom of=$M0/FILE bs=1024 count=1

# corrupt data -- append 2 bytes
echo -n "~~" >> $B0/brick0/FILE
# manually set bad-file xattr
TEST setfattr -n trusted.bit-rot.bad-file -v 0x3100 $B0/brick0/FILE

TEST $CLI volume stop $V0
TEST $CLI volume start $V0
EXPECT 'Started' volinfo_field $V0 'Status';
EXPECT_WITHIN $PROCESS_UP_TIMEOUT "1" brick_up_status $V0 $H0 $B0/brick0
EXPECT_WITHIN $PROCESS_UP_TIMEOUT "1" brick_up_status $V0 $H0 $B0/brick1
EXPECT_WITHIN $CHILD_UP_TIMEOUT "1" afr_child_up_status $V0 0
EXPECT_WITHIN $CHILD_UP_TIMEOUT "1" afr_child_up_status $V0 1
EXPECT_WITHIN $PROCESS_UP_TIMEOUT "1" get_bitd_count

# trigger lookup
TEST stat $M0/FILE

# extend the file
TEST dd if=/dev/urandom of=$M0/FILE bs=1024 count=1 oflag=append conv=notrunc

# check backend file size
EXPECT "1026" stat -c "%s" $B0/brick0/FILE
EXPECT "2048" stat -c "%s" $B0/brick1/FILE

# check file size on mount
EXPECT "2048" stat -c "%s" $M0/FILE

TEST umount $M0
cleanup
