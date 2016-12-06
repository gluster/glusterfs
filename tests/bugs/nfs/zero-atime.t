#!/bin/bash
#
# posix_do_utimes() sets atime and mtime to the values in the passed IATT. If
# not set, these values are 0 and cause a atime/mtime set to the Epoch.
#

. $(dirname $0)/../../include.rc
. $(dirname $0)/../../volume.rc
. $(dirname $0)/../../nfs.rc

cleanup

TEST glusterd
TEST pidof glusterd

TEST $CLI volume create $V0 $H0:$B0/$V0
TEST $CLI volume set $V0 nfs.disable false
TEST $CLI volume start $V0
EXPECT_WITHIN $NFS_EXPORT_TIMEOUT "1" is_nfs_export_available;
TEST mount_nfs $H0:/$V0 $N0 nolock

# create a file for testing
TEST dd if=/dev/urandom of=$M0/small count=1 bs=1024k

# timezone in UTC results in atime=0 if not set correctly
TEST TZ=UTC dd if=/dev/urandom of=$M0/small bs=64k count=1 conv=nocreat
TEST [ "$(stat --format=%X $M0/small)" != "0" ]

TEST rm $M0/small

cleanup
