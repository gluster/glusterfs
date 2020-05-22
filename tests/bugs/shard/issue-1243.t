#!/bin/bash

. $(dirname $0)/../../include.rc
. $(dirname $0)/../../volume.rc

cleanup;

TEST glusterd
TEST pidof glusterd
TEST $CLI volume create $V0 $H0:$B0/${V0}{0,1}
TEST $CLI volume set $V0 features.shard on
TEST $CLI volume set $V0 features.shard-block-size 4MB
TEST $CLI volume set $V0 performance.quick-read off
TEST $CLI volume set $V0 performance.io-cache off
TEST $CLI volume set $V0 performance.read-ahead off
TEST $CLI volume set $V0 performance.strict-o-direct on
TEST $CLI volume start $V0

TEST $GFS --volfile-id=$V0 --volfile-server=$H0 $M0

TEST $CLI volume set $V0 md-cache-timeout 10

# Write data into a file such that its size crosses shard-block-size
TEST dd if=/dev/zero of=$M0/foo bs=1048576 count=8 oflag=direct

EXPECT_WITHIN $UMOUNT_TIMEOUT "Y" force_umount $M0
TEST $GFS --volfile-id=$V0 --volfile-server=$H0 $M0

# Execute a setxattr on the file.
TEST setfattr -n trusted.libvirt -v some-value $M0/foo

# Size of the file should be the aggregated size, not the shard-block-size
EXPECT '8388608' stat -c %s $M0/foo

EXPECT_WITHIN $UMOUNT_TIMEOUT "Y" force_umount $M0
TEST $GFS --volfile-id=$V0 --volfile-server=$H0 $M0

# Execute a removexattr on the file.
TEST setfattr -x trusted.libvirt $M0/foo

# Size of the file should be the aggregated size, not the shard-block-size
EXPECT '8388608' stat -c %s $M0/foo
cleanup
