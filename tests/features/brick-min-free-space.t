#!/bin/bash
#
# Test storage.min-free-disk option works.
#

. $(dirname $0)/../include.rc
. $(dirname $0)/../volume.rc

cleanup;

TEST glusterd

TEST truncate -s 16M $B0/brick0
TEST LOOPDEV=$(losetup --find --show $B0/brick0)
TEST mkfs.xfs $LOOPDEV

mkdir -p $B0/$V0

TEST mount -t xfs $LOOPDEV $B0/$V0

###########
# AIO on  #
###########

TEST $CLI volume create $V0 $H0:$B0/$V0
TEST $CLI volume start $V0
TEST $CLI volume set $V0 readdir-ahead on
TEST $CLI vol set $V0 storage.linux-aio on

TEST glusterfs --volfile-id=$V0 --volfile-server=$H0 $M0

# Filesystem has ~12MB capacity after XFS and glusterfs overhead.
# A 16MB write should blow up.
TEST ! dd if=/dev/zero of=$M0/test bs=1M count=16 oflag=direct
TEST rm $M0/test

# But we should be able to write 10MB
TEST dd if=/dev/zero of=$M0/test bs=1M count=10 oflag=direct

# Now enable limit and set to at least 8MB free space
TEST $CLI volume set $V0 storage.freespace-check-interval 1
TEST $CLI volume set $V0 storage.min-free-disk 8388608

# Now even a tiny write ought fail.
TEST ! dd if=/dev/zero of=$M0/test1 bs=1M count=1 oflag=direct
TEST rm $M0/test1

# Repeat using percent syntax.
TEST $CLI volume set $V0 storage.min-free-disk 33%

TEST ! dd if=/dev/zero of=$M0/test1 bs=4K count=1 oflag=direct
TEST rm $M0/test1

# Disable limit.
TEST $CLI volume set $V0 storage.freespace-check-interval 0

# Now we can write again.
TEST dd if=/dev/zero of=$M0/test1 bs=4K count=1 oflag=direct

TEST rm $M0/test1
TEST rm $M0/test

EXPECT_WITHIN $UMOUNT_TIMEOUT "Y" force_umount $M0;
TEST $CLI volume stop $V0
TEST $CLI volume delete $V0

############
# AIO off  #
############

TEST $CLI volume create $V0 $H0:$B0/$V0
TEST $CLI volume start $V0
TEST $CLI volume set $V0 readdir-ahead on
TEST $CLI vol set $V0 storage.linux-aio off

TEST glusterfs --volfile-id=$V0 --volfile-server=$H0 $M0

# Filesystem has ~12MB capacity after XFS and glusterfs overhead.
# A 16MB write should blow up.
TEST ! dd if=/dev/zero of=$M0/test bs=1M count=16 oflag=direct
TEST rm $M0/test

# But we should be able to write 10MB
TEST dd if=/dev/zero of=$M0/test bs=1M count=10 oflag=direct

# Now enable limit and set to at least 8MB free space
TEST $CLI volume set $V0 storage.freespace-check-interval 1
TEST $CLI volume set $V0 storage.min-free-disk 8388608

# Now even a tiny write ought fail.
TEST ! dd if=/dev/zero of=$M0/test1 bs=1M count=1 oflag=direct
TEST rm $M0/test1

# Repeat using percent syntax.
TEST $CLI volume set $V0 storage.min-free-disk 33%

TEST ! dd if=/dev/zero of=$M0/test1 bs=4K count=1 oflag=direct
TEST rm $M0/test1

# Disable limit.
TEST $CLI volume set $V0 storage.freespace-check-interval 0

# Now we can write again.
TEST dd if=/dev/zero of=$M0/test1 bs=4K count=1 oflag=direct

TEST rm $M0/test1
TEST rm $M0/test

EXPECT_WITHIN $UMOUNT_TIMEOUT "Y" force_umount $M0;
TEST $CLI volume stop $V0
TEST $CLI volume delete $V0

cleanup;
