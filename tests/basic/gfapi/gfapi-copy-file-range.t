#!/bin/bash

. $(dirname $0)/../../include.rc
. $(dirname $0)/../../volume.rc

cleanup;

mkfs.xfs 2>&1 | grep reflink
if [ $? -ne 0 ]; then
    SKIP_TESTS
    exit
fi


TEST glusterd

TEST truncate -s 2G $B0/xfs_image
# for now, a xfs filesystem with reflink support is created.
# In future, better to make changes in MKFS_LOOP so that,
# once can create a xfs filesystem with reflink enabled in
# generic and simple way, instead of doing below steps each
# time.
TEST mkfs.xfs -f -i size=512 -m reflink=1 $B0/xfs_image;

TEST mkdir $B0/bricks
TEST mount -t xfs -o loop $B0/xfs_image $B0/bricks

# Just a single brick volume. More test cases need to be
# added in future for distribute, replicate,
# distributed replicate and distributed replicated sharded
# volumes.
TEST $CLI volume create $V0 $H0:$B0/bricks/brick1;
EXPECT 'Created' volinfo_field $V0 'Status';

TEST $CLI volume start $V0;
EXPECT 'Started' volinfo_field $V0 'Status';

TEST glusterfs --volfile-id=/$V0 --volfile-server=$H0 $M0

TEST dd if=/dev/urandom of=$M0/file bs=1M count=555;

# check for the existence of the created file
TEST stat  $M0/file;

# grab the size of the file
SRC_SIZE=$(stat -c %s $M0/file);

logdir=`gluster --print-logdir`

# TODO:
# For now, do not call copy-file-range utility. This is because,
# the regression machines are centos-7 based which does not have
# copy_file_range API available. So, instead of this testcase
# causing regression failures, for now, this is just a dummy test
# case. Uncomment the below tests (until volume stop) when there
# is support for copy_file_range in the regression machines.
#

TEST build_tester $(dirname $0)/glfs-copy-file-range.c -lgfapi

TEST ./$(dirname $0)/glfs-copy-file-range $H0 $V0 $logdir/gfapi-copy-file-range.log /file /new

# check whether the destination file is created or not
TEST stat $M0/new

# check the size of the destination file
DST_SIZE=$(stat -c %s $M0/new);

# The sizes of the source and destination should be same.
# Atleast it ensures that, copy_file_range API is working
# as expected. Whether the actual cloning happened via reflink
# or a read/write happened is different matter.
TEST [ $SRC_SIZE == $DST_SIZE ];

cleanup_tester $(dirname $0)/glfs-copy-file-range

TEST $CLI volume stop $V0
TEST $CLI volume delete $V0

UMOUNT_LOOP $B0/bricks;

cleanup;
