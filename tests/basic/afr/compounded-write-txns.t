#!/bin/bash
. $(dirname $0)/../../include.rc
. $(dirname $0)/../../volume.rc

cleanup

TEST glusterd
TEST pidof glusterd
TEST $CLI volume create $V0 replica 3 $H0:$B0/${V0}{0,1,2}
TEST $CLI volume set $V0 write-behind off
TEST $CLI volume set $V0 client-io-threads off
TEST $CLI volume start $V0
TEST $GFS --volfile-id=$V0 --volfile-server=$H0 $M0

# Create and generate data into a src file

TEST `printf %1024s |tr " " "1" > /tmp/source`
TEST `printf %1024s |tr " " "2" >> /tmp/source`

TEST dd if=/tmp/source of=$M0/file bs=1024 count=2 2>/dev/null
md5sum_file=$(md5sum $M0/file | awk '{print $1}')

TEST $CLI volume set $V0 cluster.use-compound-fops on

TEST dd if=$M0/file of=$M0/file-copy bs=1024 count=2 2>/dev/null

EXPECT_WITHIN $UMOUNT_TIMEOUT "Y" force_umount $M0
TEST $GFS --volfile-id=$V0 --volfile-server=$H0 $M0

EXPECT "$md5sum_file" echo `md5sum $M0/file-copy | awk '{print $1}'`

EXPECT_WITHIN $UMOUNT_TIMEOUT "Y" force_umount $M0
TEST $CLI volume stop $V0
TEST $CLI volume delete $V0

TEST rm -f /tmp/source
cleanup
