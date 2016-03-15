#!/bin/bash

. $(dirname $0)/../../include.rc
. $(dirname $0)/../../nfs.rc

cleanup;

## Start and create a volume
TEST glusterd
TEST pidof glusterd

TEST $CLI volume info;
TEST $CLI volume create $V0  $H0:$B0/$V0
TEST $CLI volume set $V0 nfs.disable false
TEST $CLI volume start $V0;

## Wait for volume to register with rpc.mountd
EXPECT_WITHIN $NFS_EXPORT_TIMEOUT "1" is_nfs_export_available;

## Mount NFS
TEST mount_nfs $H0:/$V0 $N0 nolock;

mkdir $N0/dir1;
mkdir $N0/dir2;
pushd $N0/ ;

##link created using relative path
ln -s dir1 symlink1;

##relative path contains ".."
ln -s ../dir1 dir2/symlink2;

##link created using absolute path
ln -s $N0/dir1 symlink3;

##link pointing to another symlinks
ln -s symlink1 symlink4
ln -s symlink3 symlink5

##dead links
ln -s does/not/exist symlink6

##link which contains ".." points out of glusterfs
ln -s ../../ symlink7

##links pointing to unauthorized area
ln -s .glusterfs symlink8

popd ;

##Umount the volume
EXPECT_WITHIN $UMOUNT_TIMEOUT "Y" umount_nfs $N0

## Mount and umount NFS via directory
TEST mount_nfs $H0:/$V0/dir1 $N0 nolock;
EXPECT_WITHIN $UMOUNT_TIMEOUT "Y" umount_nfs $N0

## Mount and umount NFS via symlink1
TEST mount_nfs $H0:/$V0/symlink1 $N0 nolock;
EXPECT_WITHIN $UMOUNT_TIMEOUT "Y" umount_nfs $N0

## Mount and umount NFS via symlink2
TEST  mount_nfs $H0:/$V0/dir2/symlink2 $N0 nolock;
EXPECT_WITHIN $UMOUNT_TIMEOUT "Y" umount_nfs $N0

## Mount NFS via symlink3 should fail
TEST ! mount_nfs $H0:/$V0/symlink3 $N0 nolock;

## Mount and umount NFS via symlink4
TEST  mount_nfs $H0:/$V0/symlink4 $N0 nolock;
EXPECT_WITHIN $UMOUNT_TIMEOUT "Y" umount_nfs $N0

## Mount NFS via symlink5 should fail
TEST ! mount_nfs $H0:/$V0/symlink5 $N0 nolock;

## Mount NFS via symlink6 should fail
TEST ! mount_nfs $H0:/$V0/symlink6 $N0 nolock;

## Mount NFS via symlink7 should fail
TEST ! mount_nfs $H0:/$V0/symlink7 $N0 nolock;

## Mount NFS via symlink8 should fail
TEST ! mount_nfs $H0:/$V0/symlink8 $N0 nolock;

##Similar check for udp mount
$CLI volume stop $V0
TEST $CLI volume set $V0 nfs.mount-udp on
$CLI volume start $V0

## Wait for volume to register with rpc.mountd
EXPECT_WITHIN $NFS_EXPORT_TIMEOUT "1" is_nfs_export_available;

## Mount and umount NFS via directory
TEST mount_nfs $H0:/$V0/dir1 $N0 nolock,mountproto=udp,proto=tcp;
EXPECT_WITHIN $UMOUNT_TIMEOUT "Y" umount_nfs $N0

## Mount and umount NFS via symlink1
TEST mount_nfs $H0:/$V0/symlink1 $N0 nolock,mountproto=udp,proto=tcp;
EXPECT_WITHIN $UMOUNT_TIMEOUT "Y" umount_nfs $N0

## Mount and umount NFS via symlink2
TEST  mount_nfs $H0:/$V0/dir2/symlink2 $N0 nolock,mountproto=udp,proto=tcp;
EXPECT_WITHIN $UMOUNT_TIMEOUT "Y" umount_nfs $N0

## Mount NFS via symlink3 should fail
TEST ! mount_nfs $H0:/$V0/symlink3 $N0 nolock,mountproto=udp,proto=tcp;

## Mount and umount NFS via symlink4
TEST  mount_nfs $H0:/$V0/symlink4 $N0 nolock,mountproto=udp,proto=tcp;
EXPECT_WITHIN $UMOUNT_TIMEOUT "Y" umount_nfs $N0

## Mount NFS via symlink5 should fail
TEST ! mount_nfs $H0:/$V0/symlink5 $N0 nolock,mountproto=udp,proto=tcp;

## Mount NFS via symlink6 should fail
TEST ! mount_nfs $H0:/$V0/symlink6 $N0 nolock,mountproto=udp,proto=tcp;

##symlink7 is not check here, because in udp mount ../../ resolves into root '/'

## Mount NFS via symlink8 should fail
TEST ! mount_nfs $H0:/$V0/symlink8 $N0 nolock,mountproto=udp,proto=tcp;

rm -rf $H0:$B0/
cleanup;
