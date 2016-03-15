#!/bin/bash

. $(dirname $0)/../../include.rc
. $(dirname $0)/../../nfs.rc
. $(dirname $0)/../../volume.rc

case $OSTYPE in
NetBSD)
        echo "Skip test on ACL which are not available on NetBSD" >&2
        SKIP_TESTS
        exit 0
        ;;
*)
        ;;
esac

cleanup;

TEST glusterd
TEST pidof glusterd
TEST $CLI volume create $V0 $H0:$B0/brick0
TEST $CLI volume set $V0 nfs.disable false
TEST $CLI volume start $V0

EXPECT_WITHIN $NFS_EXPORT_TIMEOUT "1" is_nfs_export_available;
TEST mount_nfs $H0:/$V0 $N0 nolock
cd $N0

# simple getfacl setfacl commands
TEST touch testfile
TEST setfacl -m u:14:r testfile
TEST getfacl testfile

cd
EXPECT_WITHIN $UMOUNT_TIMEOUT "Y" force_umount $N0
cleanup

