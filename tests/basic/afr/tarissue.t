#!/bin/bash

. $(dirname $0)/../../include.rc
. $(dirname $0)/../../volume.rc
. $(dirname $0)/../../nfs.rc

TESTS_EXPECTED_IN_LOOP=10
cleanup;

#Basic checks
TEST glusterd
TEST pidof glusterd

#Create a distributed-replicate volume
TEST $CLI volume create $V0 replica 2 $H0:$B0/${V0}{1..6};
TEST $CLI volume set $V0 cluster.consistent-metadata on
TEST $CLI volume set $V0 cluster.post-op-delay-secs 0
TEST $CLI volume set $V0 nfs.rdirplus off
TEST $CLI volume set $V0 nfs.disable false
TEST $CLI volume start $V0
EXPECT_WITHIN $NFS_EXPORT_TIMEOUT "1" is_nfs_export_available;

# Mount NFS
mount_nfs $H0:/$V0 $N0 vers=3

#Create files
TEST mkdir -p $N0/nfs/dir1/dir2
for i in {1..10}; do
    TEST_IN_LOOP dd if=/dev/urandom of=$N0/nfs/dir1/dir2/file$i bs=1024k count=1
done
TEST tar cvf /tmp/dir1.tar.gz $N0/nfs/dir1

TEST rm -f /tmp/dir1.tar.gz

EXPECT_WITHIN $UMOUNT_TIMEOUT "Y" force_umount $N0

cleanup;

#G_TESTDEF_TEST_STATUS_CENTOS6=BAD_TEST,BUG=1337791
#G_TESTDEF_TEST_STATUS_NETBSD7=BAD_TEST,BUG=1337791
